/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/look_tint.h"

#include "safe/look_chat.h"
#include "ui/rp_widget.h"
#include "ui/style/style_core.h"
#include "ui/style/style_core_palette.h"
#include "ui/widgets/buttons.h"

#include "styles/style_basic.h"

namespace Safe::Look {
namespace {

// The palette colors Telegram Desktop paints these icons with: the menu icon
// color (top bar buttons) and the compose area icon color (attach, emoji
// face and circle, microphone; their style aliases resolve to it).
constexpr auto kIconColors = std::array{
	"menuIconFg",
	"menuIconFgOver",
	"historyComposeIconFg",
	"historyComposeIconFgOver",
};

[[nodiscard]] bool DarkTheme() {
	return (st::windowBg->c.lightnessF() < 0.5);
}

[[nodiscard]] bool Whiten() {
	return FloatingChatBars() && DarkTheme();
}

[[nodiscard]] QColor White() {
	return QColor(255, 255, 255);
}

// A copy of the main palette with the icon colors white on a dark theme,
// refreshed on every palette change, and the icons drawn with it.
class Tint final {
public:
	Tint() {
		_palette.finalize();
		refresh();
		style::PaletteChanged(
		) | rpl::on_next([=] {
			refresh();
		}, _lifetime);
	}

	[[nodiscard]] style::color color(const style::color &original) const {
		const auto index = style::main_palette::indexOfColor(original);
		return (index >= 0) ? _palette.colorAtIndex(index) : original;
	}

	[[nodiscard]] const style::icon &icon(const style::icon &original) {
		const auto i = _icons.find(&original);
		if (i != end(_icons)) {
			return *i->second;
		}
		return *_icons.emplace(
			&original,
			std::make_unique<style::icon>(original.withPalette(_palette))
		).first->second;
	}

private:
	void refresh() {
		_palette = *style::main_palette::get();
		if (Whiten()) {
			for (const auto name : kIconColors) {
				_palette.setColor(QLatin1String(name), White());
			}
		}
		// The copies cache their colored pixmaps: drop them.
		for (const auto &[original, icon] : _icons) {
			icon->reset();
		}
	}

	style::palette _palette;
	base::flat_map<
		const style::icon*,
		std::unique_ptr<style::icon>> _icons;
	rpl::lifetime _lifetime;

};

[[nodiscard]] Tint &Instance() {
	static auto result = Tint();
	return result;
}

} // namespace

std::optional<QColor> PillIconColor(bool enabled) {
	if (!enabled || !Whiten()) {
		return std::nullopt;
	}
	return White();
}

void WhitenPillIcons(
		not_null<Ui::RpWidget*> owner,
		std::vector<Ui::IconButton*> buttons,
		Fn<void()> paletteChanged) {
	const auto apply = [=] {
		const auto color = PillIconColor();
		for (const auto button : buttons) {
			if (button) {
				button->setIconColorOverride(color);
			}
		}
	};
	apply();
	style::PaletteChanged(
	) | rpl::on_next([=] {
		apply();
		if (paletteChanged) {
			paletteChanged();
		}
	}, owner->lifetime());
}

style::color PillColor(const style::color &original) {
	return Instance().color(original);
}

const style::icon &PillIcon(const style::icon &original) {
	return Instance().icon(original);
}

} // namespace Safe::Look
