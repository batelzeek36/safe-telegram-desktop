/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "dialogs/dialogs_common.h"
#include "safe/safe_circles_core.h"

class History;
class Painter;

namespace style {
struct TextStyle;
} // namespace style

namespace Ui::Text {
class String;
} // namespace Ui::Text

// The pieces Crown and Orbit draw alike: faces (the profile photo, the jade
// focus ring, the unread badge, the name pill), the leaf, the tick notch, the
// chat preview. Every color comes from the active theme except JADELEAF's own
// marks (the leaf, the focus ring, the tick), which are jade like Calm's.
namespace Safe::Circles {

class Model;

// One face as a layout places it, in the page's coordinates.
struct FacePlace {
	int index = 0; // the chat in the circle
	QPointF center;
	double size = 0.; // diameter
	double maxSize = 0.; // the largest this face gets (its photo's size)
	double alpha = 1.; // the far side of an endless dial fades
	double shade = 0.; // 0 .. 1 toward the background (far away, behind)
	double depth = 0.; // paint order, higher in front
	double focus = 0.; // 0 .. 1, how far into the focus slot
	double offset = 0.; // slots from the focus
	double nameAlpha = 0.; // its name pill (the focus's neighbours)
	// Where its badge sits, clockwise from twelve (BadgeAngle).
	double badgeAngle = kBadgeShoulder;
	// The badge stays inside this distance from `dialCenter` (the Crown's
	// bezel ticks less a clear gap), pulled in along its angle (BadgeReach);
	// 0 = no limit.
	QPointF dialCenter;
	double badgeLimit = 0.;
};

[[nodiscard]] QColor Mix(const QColor &a, const QColor &b, double amount);
[[nodiscard]] QColor Faded(QColor color, double alpha);

[[nodiscard]] int LineHeightOf(const style::TextStyle &st);
// The least room a label keeps from the column's edges.
[[nodiscard]] int PageMargin();

// The theme's background under the circles, a hairline color that reads on
// it, the quiet glyph grey and the secondary text color.
[[nodiscard]] QColor BackgroundColor();
[[nodiscard]] QColor LineColor();
[[nodiscard]] QColor GlyphColor();
[[nodiscard]] QColor TextColor();
[[nodiscard]] QColor SubTextColor();
// A raised surface (the Crown's hub, the Orbit's card) and its edge.
[[nodiscard]] QColor SurfaceColor();
[[nodiscard]] QColor SurfaceEdgeColor();
[[nodiscard]] QColor JadeColor(double alpha = 1.);

// The face itself: the photo, dimmed by `place.shade`, the jade ring when
// `ring` > 0 (fading in with it, stopping short of the badge), and the
// unread badge on its shoulder (a small solid dot on a far face).
void PaintFace(
	Painter &p,
	Model &model,
	not_null<History*> history,
	const FacePlace &place,
	double ring);

// How far past a face's edge the outer edge of its jade focus ring lies.
[[nodiscard]] double FocusRingReach();

// The face wears its full badge (with the count), not a far face's dot:
// a preview beside it need not repeat the count.
[[nodiscard]] bool WearsFullBadge(const FacePlace &place);

// A quiet ring round the face under the pointer (its chat shows in the
// hub or the card meanwhile), stopping short of the face's badge as the
// focus ring does.
void PaintHoverRing(
	Painter &p,
	Model &model,
	not_null<History*> history,
	const FacePlace &place);

// A soft light under the focused face.
void PaintFocusLight(Painter &p, QPointF center, double radius, double alpha);

// A neighbour's name pill at most `maxWidth` wide: its line is cut to
// PillTextRoom(maxWidth), except a name over by no more than PillSlack()
// (less than an ellipsis and 2 pt), which stays whole and takes the room
// from the pill's padding, then from the page's margin: `margin` is the
// room the pill keeps from the page's edges then. Empty for an empty name.
struct PillSize {
	QSizeF size;
	double margin = 0.;
};
[[nodiscard]] int PillTextRoom(int maxWidth);
[[nodiscard]] int PillSlack();
[[nodiscard]] PillSize NamePillSize(
	const Ui::Text::String &name,
	int maxWidth);
void PaintNamePillIn(
	Painter &p,
	const Ui::Text::String &name,
	QRectF rect,
	double alpha);

// The unread badge as the chat list draws it, centered on `center`;
// returns its size.
QSize PaintBadgeAt(
	Painter &p,
	const Dialogs::BadgesState &state,
	QPointF center,
	double alpha);
[[nodiscard]] QSize BadgeSize(const Dialogs::BadgesState &state);

// His leaf, `size` across, turned by `lean` degrees, with its glow.
void PaintLeafAt(
	Painter &p,
	QPointF center,
	double size,
	double lean,
	double glow);

// The leaf as a thin line drawing (its edge and midrib), `size` across.
void PaintLeafOutline(
	Painter &p,
	QPointF center,
	double size,
	double lean,
	const QColor &color);

// The tick: a jade notch pointing at the focus (down by default), `width`
// wide, swelling and brightening for `pulse` (1 = a chat just passed).
void PaintNotch(
	Painter &p,
	QPointF tip,
	double width,
	double pulse,
	bool pointsDown = true);

// "No chats in this folder" / "Loading chats" in the middle of an empty dial.
void PaintEmpty(Painter &p, QRect rect, bool loading);

} // namespace Safe::Circles
