#pragma once
#include <QString>
#include <QVector>
#include "io/storage.h"

namespace notes {

struct PdfPage {
    QString name;
    DocumentData data;
};

bool exportPdf(const QString& path, const QVector<PdfPage>& pages, QString* error = nullptr);

}
