/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {
class RpWidget;
} // namespace Ui

namespace style {
struct UserpicButton;
struct InputField;
struct EmojiButton;
} // namespace style

// An open chat the way Telegram for macOS (the Swift app) draws it: no bars
// across the window, floating rounded pieces over the wallpaper instead.
// Top: a round back button, a pill with the photo, name and last seen, and
// a pill with call / search / more. Under it the pinned message as a pill.
// Bottom: a round attach button, the message field as a pill with the emoji
// button inside it, and a round send / record button. Telegram Desktop's own
// widgets stay where they are and keep working; these calls only change what
// is painted behind them (and a few margins so the pills have room).
// Colors are the theme's own bar colors, so night, day and custom themes work.
namespace Safe::Look {

// The native look is on: chat bars float (one switch for every hook).
[[nodiscard]] bool FloatingChatBars();

// --- Top bar (HistoryView::TopBarWidget) ---

// The chat photo in the top bar: 32pt in a 44pt wide button, 6pt from its
// left edge, so it sits inside the 40pt peer pill. Other styles unchanged.
[[nodiscard]] const style::UserpicButton &TopBarInfoStyle(
	const style::UserpicButton &st);

// The room kept free right of the actions pill (the first _rightTaken).
[[nodiscard]] int TopBarRightInset();

// The bars' bottom hairline width: none while the bars float.
[[nodiscard]] int TopShadowWidth(int width);

// A bar's bottom shadow geometry: empty while the bars float.
[[nodiscard]] QRect BarShadowGeometry(QRect geometry);

// Paints the back circle, the peer pill and the actions pill instead of the
// bar fill and returns true. Returns false (paints nothing) when the plain
// bar belongs there: `floating` false (a chat list title bar, selection or
// in-chat search sliding in). `rightTaken` = TopBarWidget::_rightTaken: the
// visible children right of it are the action buttons.
bool PaintTopBarPills(
	QPainter &p,
	not_null<QWidget*> bar,
	bool floating,
	QWidget *back,
	QWidget *info,
	int rightTaken);

// --- Pinned message (Ui::PinnedBar) ---

// Paints the pinned bar's background as a pill (or the plain fill when the
// bars do not float).
void PaintPinnedBack(not_null<QWidget*> wrap, QRect clip);

// Places the message bar and its right button inside the pill.
void PlacePinnedBar(not_null<Ui::RpWidget*> bar, QWidget *right, int width);

// --- Composer (HistoryWidget) ---

// The message field: transparent behind the text (the pill shows), the
// text moved in from the pill's rounded end, the native placeholder grey.
[[nodiscard]] const style::InputField &ComposeFieldStyle();

// The emoji button with no fill of its own (it sits inside the pill), its
// face and circle in the pill icon color (white on a dark theme).
[[nodiscard]] const style::EmojiButton &EmojiToggleStyle();

// HistoryWidget::drawField: paints the attach and send circles and the field
// pill (and, above them, the reply / edit / forward / link preview row as a
// rounded card) instead of the full width band. `band` is the area Telegram
// Desktop fills, `replyHeight` the height of the row above the field (0 when
// there is none).
void PaintComposeBack(
	QPainter &p,
	QRect band,
	int replyHeight,
	QWidget *attach,
	QWidget *field,
	QWidget *send);

} // namespace Safe::Look
