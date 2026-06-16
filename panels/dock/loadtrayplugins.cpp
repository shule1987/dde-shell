// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "loadtrayplugins.h"
#include "environments.h"

#include <signal.h>

#include <DConfig>

#include <QDir>
#include <QFileInfo>
#include <QTimer>
#include <QGuiApplication>

namespace dock {

namespace {

QString trayLoaderFontSyncPath()
{
    const QStringList candidates = {
        qEnvironmentVariable("TRAY_LOADER_FONT_SYNC_PATH"),
        QString::fromLatin1(TRAY_LOADER_FONT_SYNC_BUILD_PATH),
        QString::fromLatin1(TRAY_LOADER_FONT_SYNC_INSTALL_PATH)
    };

    for (const QString &candidate : candidates) {
        if (!candidate.isEmpty() && QFileInfo::exists(candidate)) {
            return candidate;
        }
    }

    return QString();
}

} // namespace

LoadTrayPlugins::LoadTrayPlugins(QObject *parent)
    : QObject(parent)
{

}

LoadTrayPlugins::~LoadTrayPlugins()
{
    m_shuttingDown = true;
    for (auto &pInfo : m_processes) {
        if (pInfo.process) {
            pInfo.process->kill();
            //pInfo.process->waitForFinished();
            pInfo.process->deleteLater();
        }
    }
}

void LoadTrayPlugins::loadDockPlugins()
{
    QString validExePath = loaderPath();
    if (validExePath.isEmpty()) {
        qWarning() << "No valid loader executable path found.";
        return;
    }

    auto pluginGroupMap = groupPlugins(allPluginPaths());
    for (auto it = pluginGroupMap.begin(); it != pluginGroupMap.end(); ++it) {
        if (it.value().isEmpty()) continue;
        qDebug() << "Load plugin:" << it.value() << " group:" << it.key();
        startProcess(validExePath, it.value(), it.key());
    }
}

void LoadTrayPlugins::handleProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    auto *process = qobject_cast<QProcess*>(sender());
    if (!process) return;

    if (m_shuttingDown) {
        return;
    }

    for (auto it = m_processes.begin(); it != m_processes.end(); ++it) {
        if (it->process == process) {
            if (it->retryCount < m_maxRetries) {
                it->retryCount++;
                qWarning() << "Tray plugin loader exited, restarting group:" << it->groupName
                           << "plugins:" << it->pluginPath
                           << "code:" << exitCode
                           << "exitStatus:" << exitStatus;
                QTimer::singleShot(500, process, [ this, process ] {
                    if (m_shuttingDown || process->state() != QProcess::NotRunning) {
                        return;
                    }
                    setProcessEnv(process);
                    process->start();
                });
            } else {
                qWarning() << "Maximum retries reached for tray group:" << it->groupName
                           << "plugins:" << it->pluginPath;
                process->deleteLater();
                m_processes.erase(it);
            }
            break;
        }
    }
}

void LoadTrayPlugins::startProcess(const QString &loaderPath, const QString &pluginPath, const QString &groupName)
{
    auto *process = new QProcess(this);
    process->setProcessChannelMode(QProcess::ForwardedChannels);
    setProcessEnv(process);

    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &LoadTrayPlugins::handleProcessFinished);
    connect(process, &QProcess::errorOccurred, this, [groupName, pluginPath](QProcess::ProcessError error) {
        qWarning() << "Tray plugin loader error, group:" << groupName
                   << "plugins:" << pluginPath
                   << "error:" << error;
    });
    connect(process, &QProcess::started, this, [this, process] {
        QTimer::singleShot(5000, process, [this, process] {
            if (m_shuttingDown || process->state() != QProcess::Running) {
                return;
            }

            for (auto &processInfo : m_processes) {
                if (processInfo.process == process) {
                    processInfo.retryCount = 0;
                    break;
                }
            }
        });
    });

    ProcessInfo pInfo = { process, pluginPath, groupName, 0 };
    m_processes.append(pInfo);

    process->setProgram(loaderPath);
    process->setArguments({"-p", pluginPath, "-g", groupName, "-platform", "wayland"});
    qInfo() << "Starting tray plugin loader, group:" << groupName << "plugins:" << pluginPath;
    process->start();
}

void LoadTrayPlugins::setProcessEnv(QProcess *process)
{
    if (!process) return;

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    // TODO: use protocols to determine the environment instead of environment variables
    env.remove("DDE_CURRENT_COMPOSITOR");

    const QString fontSyncPath = trayLoaderFontSyncPath();
    if (!fontSyncPath.isEmpty()) {
        QStringList preloadEntries = env.value(QStringLiteral("LD_PRELOAD")).split(QLatin1Char(':'), Qt::SkipEmptyParts);
        if (!preloadEntries.contains(fontSyncPath)) {
            preloadEntries.prepend(fontSyncPath);
        }
        env.insert(QStringLiteral("LD_PRELOAD"), preloadEntries.join(QLatin1Char(':')));
    }

    process->setProcessEnvironment(env);
}

QString LoadTrayPlugins::loaderPath() const
{
    QStringList execPaths;
    execPaths << qEnvironmentVariable("TRAY_LOADER_EXECUTE_PATH")
              << QString("%1/trayplugin-loader").arg(CMAKE_INSTALL_FULL_LIBEXECDIR)
              << QStringLiteral("/usr/libexec/trayplugin-loader");

    QString validExePath;
    for (const QString &execPath : execPaths) {
        if (QFile::exists(execPath)) {
            validExePath = execPath;
            break;
        }
    }

    return validExePath;
}

QStringList LoadTrayPlugins::allPluginPaths() const
{
    QStringList dirs;
    const auto pluginsPath = qEnvironmentVariable("TRAY_DEBUG_PLUGIN_PATH");
    if (!pluginsPath.isEmpty())
        dirs << pluginsPath.split(QDir::listSeparator());

    if (dirs.isEmpty())
        dirs << pluginDirs;

    QStringList pluginPaths;
    for (auto &pluginDir : dirs) {
        QDir dir(pluginDir);
        if (!dir.exists()) {
            qWarning() << "The plugin directory does not exist:" << pluginDir;
            continue;
        }

        auto pluginFileInfos = dir.entryInfoList({"*.so"}, QDir::Files);
        for (auto &pluginInfo : pluginFileInfos) {
            pluginPaths.append(pluginInfo.absoluteFilePath());
        }
    }

    return pluginPaths;
}

QMap<QString, QString> LoadTrayPlugins::groupPlugins(const QStringList &pluginPaths) const
{
    const QString selfMaintenancePluginsKey = "selfMaintenanceTrayPlugins";
    const QString subprojectPluginsKey = "subprojectTrayPlugins";
    const QString crashPronePluginsKey = "crashProneTrayPlugins";
    const QString otherPluginsKey = "otherTrayPlugins";
    const QSet<QString> isolatedPluginNames = {
        QStringLiteral("libdatetime.so")
    };

    auto dConfig = Dtk::Core::DConfig::create("org.deepin.dde.shell", "org.deepin.ds.dock.tray", QString());
    QStringList selfMaintenanceTrayPlugins = dConfig->value(selfMaintenancePluginsKey).toStringList();
    QStringList subprojectTrayPlugins = dConfig->value(subprojectPluginsKey).toStringList();
    QStringList crashProneTrayPlugins = dConfig->value(crashPronePluginsKey).toStringList();
    dConfig->deleteLater();

    QStringList selfMaintenancePluginPaths;
    QStringList subprojectPluginPaths;
    QStringList crashPronePluginPaths;
    QStringList otherPluginPaths;
    QList<QPair<QString, QString>> isolatedPluginPaths;

    for (auto &filePath : pluginPaths) {
        QString pluginName = filePath.section("/", -1);
        if (isolatedPluginNames.contains(pluginName)) {
            isolatedPluginPaths.append({QStringLiteral("isolatedTrayPlugin:%1").arg(pluginName), filePath});
            continue;
        }

        if (crashProneTrayPlugins.contains(pluginName)) {
            crashPronePluginPaths.append(filePath);
        } else if (selfMaintenanceTrayPlugins.contains(pluginName)) {
            selfMaintenancePluginPaths.append(filePath);
        } else if (subprojectTrayPlugins.contains(pluginName)) {
            subprojectPluginPaths.append(filePath);
        } else {
            otherPluginPaths.append(filePath);
        }
    }

    QMap<QString, QString> pluginGroup;

    if (!selfMaintenancePluginPaths.isEmpty()) {
        pluginGroup.insert(selfMaintenancePluginsKey, selfMaintenancePluginPaths.join(";"));
    }

    if (!subprojectPluginPaths.isEmpty()) {
        pluginGroup.insert(subprojectPluginsKey, subprojectPluginPaths.join(";"));
    }

    if (!crashPronePluginPaths.isEmpty()) {
        pluginGroup.insert(crashPronePluginsKey, crashPronePluginPaths.join(";"));
    }

    if (!otherPluginPaths.isEmpty()) {
        pluginGroup.insert(otherPluginsKey, otherPluginPaths.join(";"));
    }

    for (const auto &isolatedPlugin : isolatedPluginPaths) {
        pluginGroup.insert(isolatedPlugin.first, isolatedPlugin.second);
    }

    return pluginGroup;
}
}
