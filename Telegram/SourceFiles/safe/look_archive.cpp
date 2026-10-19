/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/look_archive.h"

#include "dialogs/dialogs_row.h"
#include "dialogs/ui/dialogs_layout.h"
#include "safe/look_icons.h"
#include "safe/look_style.h"
#include "ui/painter.h"
#include "ui/style/style_core_scale.h"
#include "ui/unread_badge_paint.h"

#include "styles/style_basic.h"
#include "styles/style_dialogs.h"

namespace Safe::Look {
namespace {

// Measured on the native app (night theme, 2x): a 30pt pill 5pt from the
// sides with an 8pt corner radius, outlined in the list's hairline color;
// archive glyph, "Archived Chats" and a chevron centered in the glyph grey.
constexpr auto kPillHeight = 30;
constexpr auto kPillSide = 5;
constexpr auto kPillRadius = 8;
constexpr auto kGlyphSize = 20;
constexpr auto kGlyphGap = 6;
constexpr auto kChevronGap = 6;
constexpr auto kChevronWidth = 5;
constexpr auto kChevronHeight = 10;
constexpr auto kTextFontSize = 14;
constexpr auto kBadgeRight = 10;

[[nodiscard]] const style::font &TextFont() {
	static const auto result = style::font(
		style::ConvertScale(kTextFontSize),
		st::normalFont->flags(),
		st::normalFont->family());
	return result;
}

void PaintChevron(QPainter &p, QPoint topLeft, const QColor &color) {
	const auto w = style::ConvertScale(kChevronWidth);
	const auto h = style::ConvertScale(kChevronHeight);
	auto pen = QPen(color);
	pen.setWidthF(style::ConvertScaleExact(1.6));
	pen.setCapStyle(Qt::RoundCap);
	pen.setJoinStyle(Qt::RoundJoin);
	p.setPen(pen);
	p.setBrush(Qt::NoBrush);
	auto path = QPainterPath();
	path.moveTo(topLeft.x(), topLeft.y());
	path.lineTo(topLeft.x() + w, topLeft.y() + h / 2.);
	path.lineTo(topLeft.x(), topLeft.y() + h);
	p.drawPath(path);
}

} // namespace

bool ArchiveCollapsed(bool) {
	// Always the pill, whatever the collapse setting says.
	return true;
}

bool PaintArchivePill(
		QPainter &p,
		const Dialogs::BasicRow &row,
		Data::Folder *folder,
		int unread,
		const Dialogs::Ui::PaintContext &context) {
	if (!folder || context.narrow) {
		return false;
	}
	const auto rowHeight = st::dialogsImportantBarHeight;
	p.fillRect(QRect(0, 0, context.width, rowHeight), *context.currentBg);

	const auto height = std::min(
		style::ConvertScale(kPillHeight),
		rowHeight - 2 * st::lineWidth);
	const auto side = style::ConvertScale(kPillSide);
	// A touch more room above than below, as in the native list.
	const auto top = (rowHeight - height + 1) / 2;
	const auto pill = QRectF(
		side + st::lineWidth / 2.,
		top + st::lineWidth / 2.,
		context.width - 2 * side - st::lineWidth,
		height - st::lineWidth);
	const auto radius = style::ConvertScaleExact(kPillRadius);
	auto shape = QPainterPath();
	shape.addRoundedRect(pill, radius, radius);

	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	const auto &bg = *context.currentBg;
	p.setBrush(context.selected ? st::dialogsBgOver->b : bg);
	p.drawPath(shape);
	p.save();
	p.setClipPath(shape);
	row.paintRipple(p, 0, 0, context.width);
	p.restore();
	auto border = QPen(SeparatorColor());
	border.setWidth(st::lineWidth);
	p.setPen(border);
	p.setBrush(Qt::NoBrush);
	p.drawPath(shape);

	const auto color = GlyphColor();
	const auto &font = TextFont();
	const auto text = ArchivedChatsText();
	const auto glyph = style::ConvertScale(kGlyphSize);
	const auto glyphGap = style::ConvertScale(kGlyphGap);
	const auto chevronGap = style::ConvertScale(kChevronGap);
	const auto chevronWidth = style::ConvertScale(kChevronWidth);
	const auto textWidth = font->width(text);
	const auto full = glyph + glyphGap + textWidth + chevronGap + chevronWidth;
	const auto center = int(pill.center().y());
	auto left = (context.width - full) / 2;
	PaintGlyph(
		p,
		Glyph::Archive,
		QRect(left, center - glyph / 2, glyph, glyph),
		glyph,
		color);
	left += glyph + glyphGap;
	p.setFont(font);
	p.setPen(color);
	p.drawText(left, center - font->height / 2 + font->ascent, text);
	left += textWidth + chevronGap;
	PaintChevron(
		p,
		QPoint(left, center - style::ConvertScale(kChevronHeight) / 2),
		color);

	if (unread > 0) {
		auto st = Ui::UnreadBadgeStyle();
		st.muted = true;
		Ui::PaintUnreadBadge(
			p,
			QString::number(unread),
			context.width - side - style::ConvertScale(kBadgeRight),
			center - st.size / 2,
			st);
	}
	return true;
}

} // namespace Safe::Look
