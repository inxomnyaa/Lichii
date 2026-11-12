// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <QDateTime>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QStringList>
#include "game_card.h"
#include "qt_common/config/uisettings.h"
#include "qt_common/game_list/game_list_p.h"
#include "qt_common/game_list/model.h"

namespace {
// "1.9 h · v1.3.0": play time and the update the game actually runs with.
QString GameInfoText(const QModelIndex& index) {
    QStringList parts;
    const qulonglong seconds = index.siblingAtColumn(GameListModel::COLUMN_PLAY_TIME)
                                   .data(GameListItemPlayTime::PlayTimeRole)
                                   .toULongLong();
    if (seconds == 0) {
        parts << QObject::tr("New");
    } else if (seconds < 3600) {
        parts << QObject::tr("%1 min").arg(std::max<qulonglong>(1, seconds / 60));
    } else {
        parts << QObject::tr("%1 h").arg(seconds / 3600.0, 0, 'f', 1);
    }
    const QString version = index.data(GameListItemPath::AppliedUpdateRole).toString();
    if (!version.isEmpty()) {
        parts << QStringLiteral("v") + version;
    }
    return parts.join(QStringLiteral(" · "));
}

// Height of a name drawn with DrawName(), so callers can size the box around it first.
int NameHeight(const QFontMetrics& fm, const QString& text, int width, int max_lines) {
    if (UISettings::values.game_name_overflow.GetValue() == Settings::GameNameOverflow::Scroll ||
        max_lines <= 1) {
        return fm.height();
    }
    QString shown = text;
    if (UISettings::values.game_name_overflow.GetValue() == Settings::GameNameOverflow::Ellipsis) {
        shown = fm.elidedText(text, Qt::ElideRight, width * max_lines);
    }
    const int h = fm.boundingRect(QRect(0, 0, width, 10000), Qt::TextWordWrap, shown).height();
    return std::min(h, fm.lineSpacing() * (max_lines + 1));
}

// Draws a game name the way View > Long Names asks: cut off with "…", wrapped, or (selected
// tile only) scrolled back and forth. Returns the height used.
int DrawName(QPainter* painter, const QRect& rect, const QString& text, int max_lines,
             Qt::Alignment align, bool selected) {
    const QFontMetrics fm(painter->font());
    const auto overflow = UISettings::values.game_name_overflow.GetValue();

    if (overflow == Settings::GameNameOverflow::Scroll) {
        const QRect line{rect.left(), rect.top(), rect.width(), fm.height()};
        const int extra = fm.horizontalAdvance(text) - rect.width();
        if (extra <= 0 || !selected) {
            painter->drawText(line, align | Qt::AlignTop,
                              fm.elidedText(text, Qt::ElideRight, rect.width()));
        } else {
            // Wait, glide to the end at 40 px/s, wait, glide back.
            constexpr qint64 pause = 1200, ms_per_px = 25;
            const qint64 travel = extra * ms_per_px;
            const qint64 t = QDateTime::currentMSecsSinceEpoch() % (2 * (pause + travel));
            qint64 x = 0;
            if (t >= pause && t < pause + travel) {
                x = (t - pause) / ms_per_px;
            } else if (t >= pause + travel && t < 2 * pause + travel) {
                x = extra;
            } else if (t >= 2 * pause + travel) {
                x = extra - (t - 2 * pause - travel) / ms_per_px;
            }
            painter->save();
            painter->setClipRect(line);
            painter->drawText(QRect{line.left() - static_cast<int>(x), line.top(),
                                    fm.horizontalAdvance(text), line.height()},
                              Qt::AlignLeft | Qt::AlignTop, text);
            painter->restore();
        }
        return fm.height();
    }

    const int height = NameHeight(fm, text, rect.width(), max_lines);
    if (max_lines <= 1 && overflow == Settings::GameNameOverflow::Ellipsis) {
        painter->drawText(QRect{rect.left(), rect.top(), rect.width(), height}, align | Qt::AlignTop,
                          fm.elidedText(text, Qt::ElideRight, rect.width()));
        return height;
    }
    const QString shown = overflow == Settings::GameNameOverflow::Ellipsis
                              ? fm.elidedText(text, Qt::ElideRight, rect.width() * max_lines)
                              : text;
    painter->drawText(QRect{rect.left(), rect.top(), rect.width(), height},
                      align | Qt::AlignTop | Qt::TextWordWrap, shown);
    return height;
}

// The square art of a game, or nothing when it has none.
QPixmap SquareArt(const QModelIndex& index) {
    return index.data(GameListItemPath::SquareIconRole).value<QPixmap>();
}

// Dark tile with the controller glyph, for games without art.
void DrawPlaceholder(QPainter* painter, const QRectF& rect) {
    painter->fillRect(rect, QColor(0x23, 0x24, 0x2a));
    static const QImage glyph(QStringLiteral(":/lichii/icons/menu_gamepad@2x.png"));
    if (!glyph.isNull()) {
        const qreal g = std::min(rect.width(), rect.height()) * 3 / 8;
        painter->drawImage(QRectF{rect.center().x() - g / 2, rect.center().y() - g / 2, g, g}, glyph);
    }
}

// Library icon style "Cartridge": the game drawn as a game card, as tall as `area` and centred in it.
// Switch 2 games (program IDs starting with 04) get the red card. Returns the card's outline.
QRectF DrawCartridge(QPainter* painter, const QRectF& area, const QModelIndex& index) {
    const qreal h = area.height(), w = h * 0.73;
    const QRectF body{area.center().x() - w / 2, area.top(), w, h};
    const bool switch2 =
        (index.data(GameListItemPath::ProgramIdRole).toULongLong() >> 56) == 0x04;
    const QColor body_color = switch2 ? QColor(0xec, 0x2a, 0x35) : QColor(0x3a, 0x3a, 0x3f);
    const QColor rim_color = switch2 ? QColor(0xc3, 0x14, 0x1f) : QColor(0x26, 0x26, 0x2a);

    painter->save();
    painter->setPen(Qt::NoPen);
    painter->setBrush(body_color);
    painter->drawRoundedRect(body, w * 0.08, w * 0.08);

    // slot on the top rim and arrow under the label
    painter->setBrush(rim_color);
    painter->drawRoundedRect(QRectF{body.left() + w * 0.34, body.top() + h * 0.03, w * 0.32, h * 0.035},
                             2, 2);
    const qreal ax = body.center().x(), ay = body.bottom() - h * 0.045;
    painter->drawPolygon(QPolygonF{{ax - w * 0.06, ay - h * 0.025}, {ax + w * 0.06, ay - h * 0.025},
                                   {ax, ay + h * 0.01}});

    // label: red header, square art, dark strip with the title ID
    const qreal lw = w * 0.88;
    const QRectF label{body.left() + (w - lw) / 2, body.top() + h * 0.075, lw, h * 0.83};
    QPainterPath label_clip;
    label_clip.addRoundedRect(label, lw * 0.04, lw * 0.04);
    painter->setClipPath(label_clip);

    const QRectF header{label.left(), label.top(), lw, lw * 0.17};
    painter->fillRect(header, QColor(0xe6, 0x00, 0x12));
    QFont font = painter->font();
    font.setBold(true);
    font.setPixelSize(std::max(6, static_cast<int>(header.height() * 0.5)));
    painter->setFont(font);
    painter->setPen(Qt::white);
    painter->drawText(header, Qt::AlignCenter, switch2 ? QStringLiteral("SWITCH 2") : QStringLiteral("SWITCH"));

    const QRectF art{label.left(), header.bottom(), lw, lw};
    const QPixmap pixmap = SquareArt(index);
    if (pixmap.isNull()) {
        DrawPlaceholder(painter, art);
    } else {
        painter->drawPixmap(art, pixmap, QRectF(pixmap.rect()));
    }

    const QRectF strip{label.left(), art.bottom(), lw, label.bottom() - art.bottom()};
    painter->fillRect(strip, QColor(0x16, 0x16, 0x18));
    if (strip.height() >= 6) {
        QFont mono(QStringLiteral("monospace"));
        mono.setStyleHint(QFont::Monospace);
        mono.setPixelSize(std::max(5, static_cast<int>(strip.height() * 0.5)));
        painter->setFont(mono);
        painter->setPen(QColor(0xed, 0xed, 0xee));
        painter->drawText(strip, Qt::AlignCenter,
                          QStringLiteral("%1").arg(
                              index.data(GameListItemPath::ProgramIdRole).toULongLong(), 16, 16,
                              QLatin1Char('0')).toUpper());
    }
    painter->restore();
    return body;
}
} // namespace

GameCard::GameCard(QObject* parent) : QStyledItemDelegate{parent} {
    setObjectName("GameCard");
}

void GameCard::paint(QPainter* painter, const QStyleOptionViewItem& option,
                     const QModelIndex& index) const {
    if (m_columns >= 1 && UISettings::values.grid_style.GetValue() == Settings::GridStyle::Tiles) {
        paintTile(painter, option, index);
        return;
    }

    if (!index.isValid())
        return;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    constexpr int cardCornerRadius = 10;

    const QRect cardRect = getCardRect(option, index);

    QPalette palette = option.palette;
    QColor backgroundColor = palette.window().color();
    QColor borderColor = palette.dark().color();
    QColor textColor = palette.text().color();

    // highlight on select or hover
    if (option.state & QStyle::State_Selected) {
        backgroundColor = palette.highlight().color();
        borderColor = palette.highlight().color().lighter(150);
        textColor = palette.highlightedText().color();
    } else if (option.state & QStyle::State_MouseOver) {
        backgroundColor = backgroundColor.lighter(120);
    }

    painter->setBrush(backgroundColor);
    painter->setPen(QPen(borderColor, 1));
    painter->drawRoundedRect(cardRect, cardCornerRadius, cardCornerRadius);

    const u32 icon_size = UISettings::values.game_cover_size.GetValue();
    QPixmap icon_pixmap = index.data(Qt::DecorationRole).value<QPixmap>();

    QRect iconRect;
    if (UISettings::values.game_icon_style.GetValue() == Settings::GameIconStyle::Cartridge &&
        icon_size > 0) {
        // Icon style "Cartridge": the card fills the icon's height, centred in its square.
        iconRect = {cardRect.left() + (cardRect.width() - static_cast<int>(icon_size)) / 2,
                    cardRect.top() + cardMargin - 1, static_cast<int>(icon_size),
                    static_cast<int>(icon_size)};
        painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
        DrawCartridge(painter, iconRect, index);
    } else if (!icon_pixmap.isNull()) {
        QSize scaled = icon_pixmap.size();
        scaled.scale(icon_size, icon_size, Qt::KeepAspectRatio);

        iconRect = {cardRect.left() + (cardRect.width() - scaled.width()) / 2,
                    cardRect.top() + cardMargin - 1, scaled.width(), scaled.height()};

        painter->setRenderHint(QPainter::SmoothPixmapTransform, true);

        painter->save();
        QPainterPath clip_path;
        clip_path.addRoundedRect(iconRect, cardCornerRadius, cardCornerRadius);
        painter->setClipPath(clip_path);
        painter->drawPixmap(iconRect, icon_pixmap);
        painter->restore();
    } else {
        iconRect = {cardRect.left() + cardMargin, cardRect.top() + cardMargin,
                    static_cast<int>(icon_size), static_cast<int>(icon_size)};
    }

    const bool selected = option.state & QStyle::State_Selected;
    int text_top = iconRect.bottom() + cardMargin;
    QFont font = option.font;
    font.setPixelSize(std::max(11.0, std::sqrt(static_cast<double>(icon_size))));

    if (UISettings::values.show_game_name.GetValue()) {
        // The title stays within the icon's width (see View > Long Names for long ones).
        const QString title =
            index.data(Qt::DisplayRole).toString().split(QLatin1Char('\n')).first();
        painter->setPen(textColor);
        font.setBold(true);
        painter->setFont(font);
        const int max_lines =
            UISettings::values.game_name_overflow.GetValue() == Settings::GameNameOverflow::Wrap ? 4 : 2;
        text_top += DrawName(painter, QRect{iconRect.left(), text_top, iconRect.width(), 0}, title,
                             max_lines, Qt::AlignHCenter, selected);
    }

    if (UISettings::values.show_game_info.GetValue()) {
        font.setBold(false);
        painter->setFont(font);
        const QFontMetrics fm(font);
        QColor info_color = textColor;
        info_color.setAlphaF(0.7f);
        painter->setPen(info_color);
        painter->drawText(QRect{iconRect.left(), text_top + 2, iconRect.width(), fm.height()},
                          Qt::AlignHCenter | Qt::AlignTop,
                          fm.elidedText(GameInfoText(index), Qt::ElideRight, iconRect.width()));
    }

    painter->restore();
}

// Tiles grid style: the icon fills the cell edge to edge. The name and play time sit in a dark
// strip at the bottom of the icon: on every tile when "Show Game Name" / "Show Play Time" are on,
// and always on the selected or hovered tile.
void GameCard::paintTile(QPainter* painter, const QStyleOptionViewItem& option,
                         const QModelIndex& index) const {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::SmoothPixmapTransform);

    const QRect cell = getCardRect(option, index);
    const int side = std::min(cell.width(), cell.height());
    const QRect tile{cell.left() + (cell.width() - side) / 2, cell.top(), side, side};
    constexpr qreal radius = 4;

    const bool selected = option.state & QStyle::State_Selected;
    const bool hovered = option.state & QStyle::State_MouseOver;

    // The tile's shape: the square icon, or the card with the Cartridge icon style.
    const bool cartridge =
        UISettings::values.game_icon_style.GetValue() == Settings::GameIconStyle::Cartridge;
    QRectF shape = tile;
    qreal shape_radius = radius;
    if (cartridge) {
        shape = DrawCartridge(painter, tile, index);
        shape_radius = shape.width() * 0.08;
    }

    QPainterPath clip;
    clip.addRoundedRect(shape, shape_radius, shape_radius);
    painter->setClipPath(clip);

    if (!cartridge) {
        // The square original, so the tile keeps its own 4px corners whatever the theme rounds
        // icons to; games without art get a placeholder clipped the same way.
        const QPixmap icon = SquareArt(index);
        if (icon.isNull()) {
            DrawPlaceholder(painter, tile);
        } else {
            painter->drawPixmap(tile, icon);
        }
    }

    const bool show_name = UISettings::values.show_game_name.GetValue() || selected || hovered;
    const bool show_info = UISettings::values.show_game_info.GetValue() || selected || hovered;

    QFont font = option.font;
    font.setPixelSize(std::clamp(side / 11, 10, 15));
    font.setBold(true);
    const QFontMetrics fm(font);
    const int text_width = static_cast<int>(shape.width()) - 12;
    const QString title = index.data(Qt::DisplayRole).toString().split(QLatin1Char('\n')).first();
    const int max_lines =
        UISettings::values.game_name_overflow.GetValue() == Settings::GameNameOverflow::Wrap ? 3 : 1;

    const int name_height = show_name ? NameHeight(fm, title, text_width, max_lines) : 0;
    const int info_height = show_info ? fm.height() : 0;
    if (name_height + info_height > 0) {
        const int strip_height = name_height + info_height + 8;
        const QRect strip{static_cast<int>(shape.left()), static_cast<int>(shape.bottom()) + 1 - strip_height,
                          static_cast<int>(shape.width()), strip_height};
        painter->fillRect(strip, QColor(10, 10, 14, 205));

        int y = strip.top() + 4;
        if (show_name) {
            painter->setFont(font);
            painter->setPen(Qt::white);
            y += DrawName(painter, QRect{strip.left() + 6, y, text_width, 0}, title, max_lines,
                          Qt::AlignLeft, selected);
        }
        if (show_info) {
            font.setBold(false);
            painter->setFont(font);
            painter->setPen(QColor(200, 200, 208));
            painter->drawText(QRect{strip.left() + 6, y, text_width, fm.height()},
                              Qt::AlignLeft | Qt::AlignVCenter,
                              fm.elidedText(GameInfoText(index), Qt::ElideRight, text_width));
        }
    }

    painter->setClipping(false);
    if (selected) {
        // A solid border in the theme's accent: Lichii's pink (as on the status bar's renderer
        // label), otherwise the theme's highlight colour at full strength.
        QColor accent = option.palette.highlight().color();
        accent.setAlpha(255);
        if (IsLichiiTheme()) {
            accent = QColor(0xf0, 0x38, 0x6b);
        }
        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(accent, 3));
        // The 3px stroke is centred 1.5px inside the tile, so its path radius is 1.5px smaller:
        // that puts the stroke's outer edge exactly on the icon's rounded corner.
        painter->drawRoundedRect(shape.adjusted(1.5, 1.5, -1.5, -1.5), shape_radius - 1.5,
                                 shape_radius - 1.5);
    }

    painter->restore();
}

QRect GameCard::getCardRect(const QStyleOptionViewItem& option, const QModelIndex& index) const {
    const int cell_width = option.rect.width();

    const int card_width = cell_width - m_padding;

    int card_left, card_top, card_height;

    if (m_columns >= 1) {
        // grid mode

        // center everything in-line, such that the leftmost and rightmost cards
        // have ~ equal padding to the edge of the viewport
        // spacing between each card is larger, but equal to each other
        const int column = index.row() % m_columns;

        const int row_width = m_columns * cell_width;
        const int total_gap = row_width - cardMargin * 2 - m_columns * card_width;
        const int gap = (m_columns > 1) ? (total_gap / (m_columns - 1)) : 0;
        card_left =
            option.rect.left() - column * cell_width + cardMargin + column * (card_width + gap) + 4;

        // fill cell vertically
        card_top = option.rect.top() + cardMargin;
        card_height = option.rect.height() - cardMargin - 1;
    } else {
        // carousel mode
        card_left = option.rect.left() + cardMargin + 4;

        // the delegate itself takes up the full height, but the card itself
        // gets centered
        const int content_height = m_contentSize.height() - cardMargin;
        const int cell_height = option.rect.height();

        card_height = std::min(content_height, cell_height - cardMargin * 2) - 1;
        card_top = option.rect.top() + (cell_height - card_height) / 2;
    }

    return QRect(card_left, card_top, card_width - cardMargin, card_height);
}

bool GameCard::hitTest(const QPoint& point, const QModelIndex& index, const QWidget* widget,
                       const QRect& cellRect) const {
    QStyleOptionViewItem option;
    option.initFrom(widget);
    option.rect = cellRect;
    return getCardRect(option, index).contains(point);
}

QSize GameCard::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const {
    return m_size;
}

void GameCard::setSize(const QSize& newSize, const QSize& contentSize, const int padding,
                       const int columns) {
    m_size = newSize;
    m_contentSize = contentSize;
    m_padding = padding;
    m_columns = columns;
}
