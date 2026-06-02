// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-2.0-or-later

#include "listtotableproxymodel.h"

ListToTableProxyModel::ListToTableProxyModel(QObject *parent)
    : KExtraColumnsProxyModel(parent)
{
    connect(this, &ListToTableProxyModel::rolesChanged, this, [this](){
        for (int role : std::as_const(m_roles)) {
            QByteArray fallbackName(QByteArray::number(role));
            QByteArray roleName(sourceModel() ? sourceModel()->roleNames().value(role, fallbackName) : fallbackName);
            appendColumn(QString(roleName));
        }
    });

    connect(this, &ListToTableProxyModel::sourceModelChanged, this, [this](){
        for (int idx = 0; idx < m_roles.count(); idx++) {
            int role = m_roles[idx];
            QByteArray fallbackName(QByteArray::number(role));
            QByteArray roleName(sourceModel() ? sourceModel()->roleNames().value(role, fallbackName) : fallbackName);
            setExtraColumnTitle(idx, QString(roleName));
        }

        connectSourceDataChanged();
    });

    connectSourceDataChanged();
}

void ListToTableProxyModel::connectSourceDataChanged()
{
    if (m_sourceDataChangedConnection) {
        disconnect(m_sourceDataChangedConnection);
    }

    if (!sourceModel()) {
        return;
    }

    m_sourceDataChangedConnection = connect(sourceModel(),
                                            &QAbstractItemModel::dataChanged,
                                            this,
                                            [this](const QModelIndex &topLeft, const QModelIndex &bottomRight, const QList<int> &roles) {
        if (!topLeft.isValid() || !bottomRight.isValid()) {
            return;
        }

        for (int extraColumn = 0; extraColumn < m_roles.count(); ++extraColumn) {
            if (!roles.isEmpty() && !roles.contains(m_roles.at(extraColumn))) {
                continue;
            }

            const QModelIndex first = index(topLeft.row(), proxyColumnForExtraColumn(extraColumn), topLeft.parent());
            const QModelIndex last = index(bottomRight.row(), proxyColumnForExtraColumn(extraColumn), bottomRight.parent());
            Q_EMIT dataChanged(first, last, { Qt::DisplayRole });
        }
    });
}

QVariant ListToTableProxyModel::extraColumnData(const QModelIndex &parent, int row, int extraColumn, int role) const
{
    Q_UNUSED(role)
    QVariant result(data(index(row, m_sourceColumn, parent), m_roles[extraColumn]));
    if (!result.isValid()) return QStringLiteral("<invalid>");
    return result.userType() == QMetaType::QVariantList ? result.toStringList().join(',') : result;
}
