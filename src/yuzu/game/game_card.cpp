// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include "game_card.h"
#include "qt_common/config/uisettings.h"
#include "qt_common/game_list/game_list_p.h"

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
    if (!icon_pixmap.isNull()) {
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

    if (UISettings::values.show_game_name.GetValue()) {
        // Keep the title within the icon's width: at most two lines, cut with "..." beyond that.
        const QRect textRect(iconRect.left(), iconRect.bottom() + cardMargin, iconRect.width(),
                             cardRect.bottom() - iconRect.bottom() - cardMargin);

        QString title = index.data(Qt::DisplayRole).toString().split(QLatin1Char('\n')).first();

        painter->setPen(textColor);
        QFont font = option.font;
        font.setBold(true);
        font.setPixelSize(std::max(11.0, std::sqrt(static_cast<double>(icon_size))));
        painter->setFont(font);

        const QFontMetrics fm(font);
        title = fm.elidedText(title, Qt::ElideRight, textRect.width() * 2);
        painter->drawText(textRect, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, title);
    }

    painter->restore();
}

// Tiles grid style: the icon fills the cell edge to edge. The name sits in a dark strip at the
// bottom of the icon: on every tile when "Show Game Name" is on, and always on the selected or
// hovered tile.
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

    QPainterPath clip;
    clip.addRoundedRect(tile, radius, radius);
    painter->setClipPath(clip);

    // The square original, so the tile keeps its own 4px corners whatever the theme rounds icons to.
    const QPixmap icon = index.data(GameListItemPath::SquareIconRole).value<QPixmap>();
    if (icon.isNull()) {
        // No art: a plain dark tile with the controller glyph, rounded by this tile's own clip.
        painter->fillRect(tile, QColor(0x23, 0x24, 0x2a));
        static const QImage glyph(QStringLiteral(":/lichii/icons/menu_gamepad@2x.png"));
        if (!glyph.isNull()) {
            const int g = side * 3 / 8;
            painter->drawImage(QRect{tile.left() + (side - g) / 2, tile.top() + (side - g) / 2, g, g},
                               glyph);
        }
    } else {
        painter->drawPixmap(tile, icon);
    }

    if (UISettings::values.show_game_name.GetValue() || selected || hovered) {
        QFont font = option.font;
        font.setPixelSize(std::clamp(side / 11, 10, 15));
        font.setBold(true);
        const QFontMetrics fm(font);
        const int text_width = side - 12;
        const QString title =
            index.data(Qt::DisplayRole).toString().split(QLatin1Char('\n')).first();

        const int strip_height = fm.height() + 8;
        const QRect strip{tile.left(), tile.bottom() + 1 - strip_height, tile.width(), strip_height};
        painter->fillRect(strip, QColor(10, 10, 14, 205));
        painter->setFont(font);
        painter->setPen(Qt::white);
        painter->drawText(QRect{strip.left() + 6, strip.top() + 4, text_width, fm.height()},
                          Qt::AlignLeft | Qt::AlignVCenter,
                          fm.elidedText(title, Qt::ElideRight, text_width));
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
        painter->drawRoundedRect(QRectF(tile).adjusted(1.5, 1.5, -1.5, -1.5), radius - 1.5,
                                 radius - 1.5);
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
