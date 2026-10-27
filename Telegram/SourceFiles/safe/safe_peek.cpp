/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_peek.h"

#include "chat_helpers/stickers_lottie.h"
#include "core/click_handler_types.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_groups.h"
#include "data/data_media_types.h"
#include "data/data_peer.h"
#include "data/data_photo.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/media/history_view_media_common.h"
#include "history/view/media/history_view_sticker_player.h"
#include "main/main_session.h"
#include "safe/safe_calm.h"
#include "safe/safe_quiet_media.h"
#include "ui/chat/attach/attach_item_single_media_preview.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/vertical_list.h"
#include "ui/widgets/labels.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_layers.h"

namespace Safe::Peek {
namespace {

using StickerPlayer = HistoryView::StickerPlayer;

[[nodiscard]] HistoryItemsList PartsOf(not_null<HistoryItem*> item) {
	const auto media = item->media();
	if (media && media->canBeGrouped()) {
		if (const auto album = item->history()->owner().groups().find(item)) {
			return album->items;
		}
	}
	return { item };
}

[[nodiscard]] DocumentData *StickerOf(not_null<const HistoryItem*> part) {
	const auto media = part->media();
	const auto document = media ? media->document() : nullptr;
	return (document && document->sticker()) ? document : nullptr;
}

// The parts ItemSingleMediaPreview draws: photos, GIFs (and round videos,
// which it plays like GIFs), videos (their first frame) and pictures sent
// as files (their thumbnail).
[[nodiscard]] bool PreviewShows(not_null<const HistoryItem*> part) {
	const auto media = part->media();
	if (!media || media->webpage() || media->storyId().valid()) {
		return false;
	} else if (media->photo()) {
		return true;
	} else if (const auto document = media->document()) {
		return !document->sticker()
			&& (document->isAnimation()
				|| document->isVideoFile()
				|| document->isImage());
	}
	return false;
}

[[nodiscard]] bool Shows(not_null<const HistoryItem*> part) {
	return PreviewShows(part) || StickerOf(part);
}

[[nodiscard]] bool ShowsMedia(not_null<HistoryItem*> item) {
	if (!Quiet::ReplacesMedia(item)) {
		return false;
	}
	for (const auto &part : PartsOf(item)) {
		if (Shows(part)) {
			return true;
		}
	}
	return false;
}

// What she wrote, when Calm softened it (an album's caption may sit on any
// of its parts).
[[nodiscard]] std::optional<TextWithEntities> OriginalOf(
		not_null<HistoryItem*> item) {
	for (const auto &part : PartsOf(item)) {
		if (auto original = Calm::SoftenedOriginal(part)) {
			return original;
		}
	}
	return std::nullopt;
}

[[nodiscard]] QString SenderName(not_null<HistoryItem*> item) {
	const auto from = item->from();
	return from->isUser() ? from->shortName() : from->name();
}

[[nodiscard]] std::shared_ptr<StickerPlayer> MakeStickerPlayer(
		not_null<Data::DocumentMedia*> media,
		QSize box) {
	const auto info = media->owner()->sticker();
	if (info->isLottie()) {
		return std::make_shared<HistoryView::LottiePlayer>(
			ChatHelpers::LottiePlayerFromDocument(
				media,
				ChatHelpers::StickerLottieSize::MessageHistory,
				box,
				Lottie::Quality::High));
	} else if (info->isWebm()) {
		return std::make_shared<HistoryView::WebmPlayer>(
			media->owner()->location(),
			media->bytes(),
			box);
	}
	return std::make_shared<HistoryView::StaticStickerPlayer>(
		media->owner()->location(),
		media->bytes(),
		box);
}

// A sticker as the chat would have shown it, playing, in the middle of a
// row the height of a big sticker.
void AddSticker(
		not_null<Ui::GenericBox*> box,
		not_null<DocumentData*> sticker) {
	const auto size = st::maxStickerSize;
	const auto row = box->addRow(
		object_ptr<Ui::RpWidget>(box),
		style::margins(0, st::boxRowPadding.left(), 0, 0));
	row->resize(row->width(), size);

	struct State {
		std::shared_ptr<Data::DocumentMedia> media;
		std::shared_ptr<StickerPlayer> player;
		rpl::lifetime loading;
	};
	const auto state = row->lifetime().make_state<State>();
	state->media = sticker->createMediaView();
	state->media->checkStickerLarge();
	state->media->goodThumbnailWanted();

	rpl::single(rpl::empty) | rpl::then(
		sticker->session().downloaderTaskFinished()
	) | rpl::filter([=] {
		return state->media->loaded();
	}) | rpl::take(1) | rpl::on_next([=] {
		state->player = MakeStickerPlayer(
			state->media.get(),
			QSize(size, size));
		state->player->setRepaintCallback([=] { row->update(); });
		row->update();
	}, state->loading);

	row->paintRequest() | rpl::on_next([=] {
		if (!state->player || !state->player->ready()) {
			return;
		}
		auto p = QPainter(row);
		const auto frame = state->player->frame(
			QSize(size, size),
			QColor(0, 0, 0, 0),
			false,
			crl::now(),
			!row->window()->isActiveWindow()).image;
		const auto target = HistoryView::DownscaledSize(
			frame.size(),
			QSize(size, size));
		p.drawImage(
			QRect(
				(row->width() - target.width()) / 2,
				(row->height() - target.height()) / 2,
				target.width(),
				target.height()),
			frame);
		state->player->markFrameShown();
	}, row->lifetime());
}

void AddPreview(not_null<Ui::GenericBox*> box, not_null<HistoryItem*> part) {
	box->addRow(
		object_ptr<Ui::ItemSingleMediaPreview>(
			box,
			st::defaultComposeControls,
			[] { return false; }, // the box is the only thing in front
			part,
			Ui::AttachControls::Type::None),
		style::margins(0, st::boxRowPadding.left(), 0, 0));
}

} // namespace

bool Peekable(not_null<HistoryItem*> item) {
	return Quiet::Item(item)
		&& (ShowsMedia(item) || OriginalOf(item).has_value());
}

std::shared_ptr<ClickHandler> Link(not_null<HistoryItem*> item) {
	static auto links = base::flat_map<FullMsgId, ClickHandlerPtr>();
	const auto id = item->fullId();
	if (const auto i = links.find(id); i != end(links)) {
		return i->second;
	}
	auto result = std::make_shared<LambdaClickHandler>([=](
			ClickContext context) {
		const auto my = context.other.value<ClickHandlerContext>();
		const auto window = my.sessionWindow.get();
		if (!window) {
			return;
		} else if (const auto item = window->session().data().message(id)) {
			if (Peekable(item)) {
				window->show(Box(PeekBox, item));
			}
		}
	});
	links.emplace(id, result);
	return result;
}

void PeekBox(not_null<Ui::GenericBox*> box, not_null<HistoryItem*> item) {
	const auto name = SenderName(item);
	const auto media = ShowsMedia(item);
	const auto original = OriginalOf(item);
	box->setTitle(rpl::single((media ? u"What %1 sent"_q : u"What %1 said"_q)
		.arg(name)));
	box->setWidth(st::boxWideWidth);
	box->addButton(rpl::single(u"Close"_q), [=] { box->closeBox(); });

	if (media) {
		for (const auto &part : PartsOf(item)) {
			if (const auto sticker = StickerOf(part)) {
				AddSticker(box, sticker);
			} else if (PreviewShows(part)) {
				AddPreview(box, part);
			}
		}
	}
	if (original) {
		if (media) {
			Ui::AddSkip(box->verticalLayout());
		}
		const auto label = box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			rpl::single(*original),
			st::boxLabel));
		label->setSelectable(true);
	}
	Ui::AddSkip(box->verticalLayout());
	Ui::AddDividerText(
		box->verticalLayout(),
		rpl::single(original
			? u"Only you can see this. The chat shows the calm one."_q
			: u"Only you can see this. The chat stays quiet."_q));
}

} // namespace Safe::Peek
