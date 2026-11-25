#pragma once
#include <QString>

class SessionStore;
class ImageLibrary;

// User-controlled local complete backup. Legacy .emberbackup stays supported.
// This format is checksum-verified but deliberately NOT encrypted.
namespace EmberBundle {
bool exportComplete(SessionStore &store, ImageLibrary &images,
                    const QString &path, QString *error = nullptr);
bool restoreComplete(SessionStore &store, ImageLibrary &images,
                     const QString &path, QString *error = nullptr);
}
