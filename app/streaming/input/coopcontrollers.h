#pragma once

#include "SDL_compat.h"

#include <QSet>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

// SDL's device index is only valid until another device is attached or removed.
// A joystick path is stable for the lifetime of a physical controller and is
// therefore the identity used by couch co-op workers.
namespace CoopControllers {

inline QString pathForDeviceIndex(int deviceIndex)
{
#if SDL_VERSION_ATLEAST(2, 0, 14)
    const char* path = SDL_JoystickPathForIndex(deviceIndex);
    return path == nullptr ? QString() : QString::fromUtf8(path);
#else
    Q_UNUSED(deviceIndex);
    return QString();
#endif
}

inline QVariantList enumerate()
{
    QVariantList controllers;
    QSet<QString> paths;

    // The picker is created before the normal GUI gamepad navigation object,
    // so initialize SDL temporarily when no other component owns it yet.
    const bool initializeJoystick = (SDL_WasInit(SDL_INIT_JOYSTICK) == 0);
    const bool initializeController = (SDL_WasInit(SDL_INIT_GAMECONTROLLER) == 0);
    if (initializeJoystick && SDL_InitSubSystem(SDL_INIT_JOYSTICK) != 0) {
        return controllers;
    }
    if (initializeController && SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) {
        if (initializeJoystick) {
            SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
        }
        return controllers;
    }

    const int joystickCount = SDL_NumJoysticks();
    for (int deviceIndex = 0; deviceIndex < joystickCount; ++deviceIndex) {
        if (!SDL_IsGameController(deviceIndex)) {
            continue;
        }

        const QString path = pathForDeviceIndex(deviceIndex);
        if (path.isEmpty() || paths.contains(path)) {
            continue;
        }

        paths.insert(path);
        QVariantMap controller;
        const char* name = SDL_JoystickNameForIndex(deviceIndex);
        controller.insert(QStringLiteral("name"),
                          name == nullptr ? QStringLiteral("Controller") : QString::fromUtf8(name));
        controller.insert(QStringLiteral("path"), path);
        controllers.append(controller);
    }

    if (initializeController) {
        SDL_FlushEvents(SDL_CONTROLLERDEVICEADDED, SDL_CONTROLLERDEVICEREMAPPED);
        SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
    }
    if (initializeJoystick) {
        SDL_FlushEvents(SDL_JOYDEVICEADDED, SDL_JOYDEVICEREMOVED);
        SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
    }

    return controllers;
}

} // namespace CoopControllers
