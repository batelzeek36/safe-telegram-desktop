/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "dialogs/dialogs_common.h"

class History;
class Painter;

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
};

[[nodiscard]] QColor Mix(const QColor &a, const QColor &b, double amount);
[[nodiscard]] QColor Faded(QColor color, double alpha);

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
// `ring` > 0 (fading in with it), and the unread badge on its shoulder.
void PaintFace(
	Painter &p,
	Model &model,
	not_null<History*> history,
	const FacePlace &place,
	double ring);

// A soft light under the focused face.
void PaintFocusLight(Painter &p, QPointF center, double radius, double alpha);

// A name pill's rectangle centered on `center`, at most `maxWidth` wide
// (empty for an empty name), and the pill drawn in it.
[[nodiscard]] QRectF NamePillRect(
	const Ui::Text::String &name,
	QPointF center,
	int maxWidth);
void PaintNamePillIn(
	Painter &p,
	const Ui::Text::String &name,
	QRectF rect,
	double alpha);

// The short name in a small pill sitting on the bottom edge of a face.
void PaintNamePill(
	Painter &p,
	const Ui::Text::String &name,
	QPointF faceBottom,
	int maxWidth,
	double alpha);

// The unread badge as the chat list draws it, centered on `center`, with
// a ring of the background around it; returns its size.
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

// The tick: a jade notch pointing at the focus (down by default), `width`
// wide, swelling and brightening for `pulse` (1 = a chat just passed).
void PaintNotch(
	Painter &p,
	QPointF tip,
	double width,
	double pulse,
	bool pointsDown = true);

// The focused chat's preview centered in `rect`: name, the last message line
// (one line each, cut to fit), then time and badge. Returns the height used.
int PaintCenteredPreview(
	Painter &p,
	Model &model,
	not_null<History*> history,
	QRect rect,
	bool measureOnly = false);

// The preview as a chat list row without the photo: name and time on the
// first line, the message and the badge on the second, inside `rect`.
void PaintRowPreview(
	Painter &p,
	Model &model,
	not_null<History*> history,
	QRect rect);

// "No chats in this folder" / "Loading chats" in the middle of an empty dial.
void PaintEmpty(Painter &p, QRect rect, bool loading);

} // namespace Safe::Circles
