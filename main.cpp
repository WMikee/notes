#include "mainwindow.h"
#include <QApplication>
#include <QFont>
#include <QFontDatabase>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QSurfaceFormat>

namespace {
void applyAppFont(QApplication& app)
{
    const int id = QFontDatabase::addApplicationFont(":/assets/fonts/Lexend-Variable.ttf");
    if (id < 0) return;
    const QStringList families = QFontDatabase::applicationFontFamilies(id);
    if (families.isEmpty()) return;
    QFont font = app.font();
    font.setFamilies(families);
    app.setFont(font);
}

void enableInputMethod()
{
    if (!qEnvironmentVariableIsEmpty("QT_IM_MODULE")) return;
    const QStringList candidates = {QStringLiteral("ibus"), QStringLiteral("fcitx")};
    for (const QString& im : candidates) {
        if (QStandardPaths::findExecutable(im).isEmpty()) continue;
        qputenv("QT_IM_MODULE", im.toUtf8());
        if (qEnvironmentVariable("XMODIFIERS", QStringLiteral("@im=none")) == QLatin1String("@im=none"))
            qputenv("XMODIFIERS", QStringLiteral("@im=%1").arg(im).toUtf8());
        return;
    }
}
}

int main(int argc, char** argv)
{
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setSamples(8);
    QSurfaceFormat::setDefaultFormat(fmt);

    enableInputMethod();

    QApplication app(argc, argv);
    applyAppFont(app);
    notes::NotesWindow w;
    w.resize(1000, 700);
    w.show();
    return app.exec();
}
