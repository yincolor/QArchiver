// QArchiver / 小Q解压
// GitHub release update checker.

#include "ui/services/UpdateChecker.h"

#include <QCoreApplication>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTextDocument>
#include <QUrl>
#include <QXmlStreamReader>
#include <QVersionNumber>

namespace {

const QString kLatestReleaseUrl =
    QStringLiteral("https://github.com/yincolor/QArchiver/releases.atom");

struct ParsedVersion {
    QVersionNumber numeric;
    int revision = 0;
    bool valid = false;
};

ParsedVersion parseVersion(const QString &value)
{
    ParsedVersion parsed;
    QString text = value.trimmed();
    if (text.startsWith(QLatin1Char('v'), Qt::CaseInsensitive)) {
        text.remove(0, 1);
    }
    const int dash = text.indexOf(QLatin1Char('-'));
    const QString numericPart = dash >= 0 ? text.left(dash) : text;
    const QString revisionPart = dash >= 0 ? text.mid(dash + 1) : QString();
    parsed.numeric = QVersionNumber::fromString(numericPart);
    if (parsed.numeric.isNull()) {
        return parsed;
    }
    if (!revisionPart.isEmpty()) {
        bool ok = false;
        const int revision = revisionPart.toInt(&ok);
        parsed.revision = ok ? revision : 0;
    }
    parsed.valid = true;
    return parsed;
}

} // namespace

UpdateChecker &UpdateChecker::instance()
{
    static UpdateChecker checker;
    return checker;
}

UpdateChecker::UpdateChecker()
    : m_network(new QNetworkAccessManager(this))
{
}

bool UpdateChecker::isNewerVersion(const QString &candidate,
                                   const QString &current)
{
    const ParsedVersion latest = parseVersion(candidate);
    const ParsedVersion installed = parseVersion(current);
    if (!latest.valid || !installed.valid) {
        return false;
    }
    const int comparison = QVersionNumber::compare(latest.numeric,
                                                   installed.numeric);
    if (comparison != 0) {
        return comparison > 0;
    }
    return latest.revision > installed.revision;
}

void UpdateChecker::check(bool silent)
{
    if (m_inFlight) {
        return;
    }
    m_inFlight = true;
    m_silent = silent;

    QNetworkRequest request{QUrl(kLatestReleaseUrl)};
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("QArchiver/%1")
                          .arg(QCoreApplication::applicationVersion()));
    request.setRawHeader("Accept", "application/atom+xml");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(15000);

    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        m_inFlight = false;
        const QByteArray payload = reply->readAll();
        const int status =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QNetworkReply::NetworkError error = reply->error();
        const QString errorText = reply->errorString();
        reply->deleteLater();

        if (error != QNetworkReply::NoError || status != 200) {
            QString message = errorText;
            if (status == 404) {
                message = tr("No release information was found.");
            }
            if (!m_silent) {
                emit checkFailed(message);
            }
            return;
        }

        // GitHub's releases Atom feed is public and not subject to the API
        // rate limit. The first entry is the latest release.
        // GitHub Release Atom 源是公开的，且不受 API 频率限制；第一条即最新版。
        QXmlStreamReader xml(payload);
        QString title;
        QString releaseUrl;
        QString notes;
        bool inEntry = false;
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isStartElement()) {
                const QString name = xml.name().toString();
                if (name == QLatin1String("entry")) {
                    inEntry = true;
                } else if (inEntry && name == QLatin1String("title")) {
                    title = xml.readElementText();
                } else if (inEntry && name == QLatin1String("link")
                           && releaseUrl.isEmpty()) {
                    const QString href =
                        xml.attributes().value(QStringLiteral("href")).toString();
                    if (!href.isEmpty()) {
                        releaseUrl = href;
                    }
                } else if (inEntry && (name == QLatin1String("content")
                                       || name == QLatin1String("summary"))) {
                    notes = xml.readElementText();
                }
            } else if (xml.isEndElement()
                       && xml.name() == QLatin1String("entry")) {
                break;
            }
        }
        if (xml.hasError() || releaseUrl.isEmpty()) {
            if (!m_silent) {
                emit checkFailed(tr("The update server returned invalid data."));
            }
            return;
        }
        const QString tag = releaseUrl.section(QLatin1Char('/'), -1);
        if (tag.isEmpty()) {
            if (!m_silent) {
                emit checkFailed(tr("The update server returned invalid data."));
            }
            return;
        }
        if (notes.contains(QLatin1Char('<'))) {
            QTextDocument document;
            document.setHtml(notes);
            notes = document.toPlainText();
        }
        notes = notes.trimmed();
        if (notes.size() > 4000) {
            notes = notes.left(4000) + QStringLiteral("...");
        }
        Q_UNUSED(title);
        if (isNewerVersion(tag, QCoreApplication::applicationVersion())) {
            emit updateAvailable(tag, releaseUrl, notes);
        } else {
            emit upToDate(QCoreApplication::applicationVersion());
        }
    });}
