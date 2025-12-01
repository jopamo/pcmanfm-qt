/*
 * XDG directory utilities implementation
 * pcmanfm/xdgdir.cpp
 */

#include "xdgdir.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

static const QRegularExpression desktopRegex(QStringLiteral("XDG_DESKTOP_DIR=\"([^\n]*)\""));

QString XdgDir::readUserDirsFile() {
    QFile file(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QStringLiteral("/user-dirs.dirs"));
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QByteArray data = file.readAll();
        file.close();
        return QString::fromLocal8Bit(data);
    }
    return QString();
}

QString XdgDir::readDesktopDir() {
    QString str = readUserDirsFile();
    if (!str.isEmpty()) {
        QRegularExpressionMatch match;
        if (str.lastIndexOf(desktopRegex, -1, &match) != -1) {
            str = match.captured(1);
            if (str.startsWith(QStringLiteral("$HOME"))) {
                str = QStandardPaths::writableLocation(QStandardPaths::HomeLocation) + str.mid(5);
            }
            return str;
        }
    }
    return QStandardPaths::writableLocation(QStandardPaths::HomeLocation) + QStringLiteral("/Desktop");
}

void XdgDir::setDesktopDir(QString path) {
    QString home = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    if (path.startsWith(home)) {
        path = QStringLiteral("$HOME") + path.mid(home.length());
    }
    QString str = readUserDirsFile();
    QString line = QStringLiteral("XDG_DESKTOP_DIR=\"") + path + QLatin1Char('\"');
    if (str.contains(desktopRegex)) {
        str.replace(desktopRegex, line);
    }
    else {
        if (!str.endsWith(QLatin1Char('\n'))) {
            str += QLatin1Char('\n');
        }
        str += line + QLatin1Char('\n');
    }
    QString dir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    if (QDir().mkpath(dir)) {  // write the file
        QSaveFile file(dir + QStringLiteral("/user-dirs.dirs"));
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            file.write(str.toLocal8Bit());
            file.commit();
        }
    }
}
