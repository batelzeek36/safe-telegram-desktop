/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_judge_image.h"

#include <QtCore/QBuffer>

namespace Safe::JudgeImage {
namespace {

constexpr auto kJpegQuality = 85;

} // namespace

std::string Encode(const QImage &image) {
	if (image.isNull() || image.width() <= 0 || image.height() <= 0) {
		return std::string();
	}
	const auto side = JudgeProtocol::kMaxSide;
	auto scaled = (std::max(image.width(), image.height()) > side)
		? image.scaled(
			QSize(side, side),
			Qt::KeepAspectRatio,
			Qt::SmoothTransformation)
		: image;
	if (scaled.isNull()) {
		return std::string();
	}
	scaled.setDevicePixelRatio(1.);
	auto flat = QImage(scaled.size(), QImage::Format_RGB32);
	flat.setDevicePixelRatio(1.);
	flat.fill(Qt::white);
	{
		auto p = QPainter(&flat);
		p.drawImage(0, 0, scaled);
	}
	auto bytes = QByteArray();
	auto buffer = QBuffer(&bytes);
	if (!buffer.open(QIODevice::WriteOnly)
		|| !flat.save(&buffer, "JPG", kJpegQuality)) {
		return std::string();
	}
	buffer.close();
	if (bytes.isEmpty()
		|| std::size_t(bytes.size()) > JudgeProtocol::kMaxPictureBytes) {
		return std::string();
	}
	return bytes.toStdString();
}

std::string ClipText(const QString &text) {
	const auto limit = qsizetype(JudgeProtocol::kMaxTextUnits);
	if (text.size() <= limit) {
		return text.toStdString();
	}
	auto clipped = text.left(limit);
	if (clipped.back().isHighSurrogate()) {
		clipped.chop(1);
	}
	return clipped.toStdString();
}

JudgeProtocol::Request Prepare(
		bool document,
		const QImage &cover,
		const std::vector<QImage> &frames,
		const QString &caption,
		const QString &fileName,
		const QString &mime) {
	auto result = JudgeProtocol::Request();
	result.kind = JudgeProtocol::KindFor(document, mime.toStdString());
	result.cover = Encode(cover);
	if (result.cover.empty()) {
		return result;
	}
	result.coverStripped = JudgeProtocol::LooksStripped(
		cover.width(),
		cover.height());
	for (const auto &frame : frames) {
		if (int(result.frames.size()) >= JudgeProtocol::kMaxFrames) {
			break;
		}
		auto encoded = Encode(frame);
		if (!encoded.empty()) {
			result.frames.push_back(std::move(encoded));
		}
	}
	result.caption = ClipText(caption);
	result.fileName = ClipText(fileName);
	result.mime = ClipText(mime);
	return result;
}

} // namespace Safe::JudgeImage
