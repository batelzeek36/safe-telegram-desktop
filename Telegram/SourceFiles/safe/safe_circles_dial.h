/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "safe/safe_circles_core.h"
#include "safe/safe_circles_paint.h"

class History;
class Painter;

// The two dial layouts, each a geometry plus its own drawing:
//
// Crown (Apple Watch's Friends wheel, the Galaxy Watch bezel): one ring of
// faces round a hub, a bezel of ticks that turns with it, the focus at
// twelve o'clock under a jade notch, the focused chat's preview in the hub.
//
// Orbit (Apple Watch's Astronomy face, Cover Flow): his leaf in the middle,
// the faces on a tilted orbit round it, the front one large in a pool of
// light with the notch above it, its preview in a card under the orbit.
//
// A page is one circle; the view slides pages sideways and draws the header
// and the dots, a Dial draws everything inside a page.
namespace Safe::Circles {

class Model;

struct DialFrame {
	double position = 0.;
	int count = 0;
	double lean = 0.; // degrees, + = clockwise
	double pulse = 0.; // the tick, 1 = a chat just reached the focus
	int hovered = -1; // the chat under the pointer
	double names = 1.; // name pills: 0 while the dial moves, back after it lands
	History *focused = nullptr;
	// The chat the hub (Crown) or the card (Orbit) shows: the one under the
	// pointer, else the focused one; fading in for `previewAlpha` after
	// the pointer moves onto a face or off it.
	History *previewed = nullptr;
	double previewAlpha = 1.;
	// It is the chat at the focus, or the one a glide is bringing there:
	// its face wears (or is about to wear) its unread count.
	bool previewFocus = false;
	// The chat the preview showed before, fading out under the new one
	// (a crossfade, never an empty hub or card in between).
	History *previewedOld = nullptr;
	double previewOldAlpha = 0.;
	bool loading = false;
	bool keyboard = false; // the chat list has the keyboard focus
};

class Dial {
public:
	virtual ~Dial() = default;

	[[nodiscard]] virtual Layout layout() const = 0;
	virtual void resize(QSize size) = 0;

	// The faces at `position`, in paint order (back to front).
	[[nodiscard]] virtual std::vector<FacePlace> place(
		double position,
		int count) const = 0;

	// The whole page: the faces of `chats` at `frame.position` with what
	// lies under them (bezel, orbit, leaf, light) and over them (hub, card,
	// notch).
	virtual void paint(
		Painter &p,
		Model &model,
		const std::vector<not_null<History*>> &chats,
		const DialFrame &frame) = 0;

	// The chat whose face is under `point` (front faces first), or -1.
	[[nodiscard]] int faceAt(QPointF point, double position, int count) const;

	// A press here can spin the dial (elsewhere a sideways drag changes
	// circle); how many chats a pointer move turns it; a click here opens
	// the focused chat (the hub, the card).
	[[nodiscard]] virtual bool grabs(QPointF point) const = 0;
	[[nodiscard]] virtual double turn(
		QPointF from,
		QPointF to,
		int count) const = 0;
	[[nodiscard]] virtual bool opens(QPointF point) const = 0;

	// Two finger scrolling: pixels of scroll for one chat.
	[[nodiscard]] virtual double pixelsPerChat() const = 0;
	// The widest a name pill may be.
	[[nodiscard]] virtual int pillWidth() const = 0;
};

[[nodiscard]] std::unique_ptr<Dial> MakeCrown();
[[nodiscard]] std::unique_ptr<Dial> MakeOrbit();

// Shared by both layouts.
[[nodiscard]] double Smooth(double t);
[[nodiscard]] double FocusAmount(double offset);
[[nodiscard]] double NameAlpha(double offset, double reach);

} // namespace Safe::Circles
