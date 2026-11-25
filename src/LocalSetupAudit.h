#pragma once
#include <QString>

namespace EmberLocalSetup {
// This inspection reads only small native file headers and metadata.
// It does not execute any supplied model engine or make network requests.
struct Inputs {
    QString brainExe;
    QString brainModel;
    QString whisperExe;
    QString whisperModel;
    bool whisperSelected = true;
    bool microphoneDetected = false;
};
struct Report {
    bool textFilesReady = false;
    bool dictationFilesReady = false;
    QString text;
};
Report inspect(const Inputs &input);
} // namespace EmberLocalSetup
