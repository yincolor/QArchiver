#ifndef PASSWORDDIALOG_H
#define PASSWORDDIALOG_H

// QArchiver / 小Q解压
// Password prompt with a per-session remember checkbox.
// 带会话内记住复选框的密码输入对话框。

#include <QDialog>

class QLineEdit;
class QCheckBox;

// Password input dialog (design doc - opened when encrypted archive detected).
// 密码输入对话框（检测到加密压缩包时打开）。
class PasswordDialog : public QDialog {
    Q_OBJECT
public:
    explicit PasswordDialog(QWidget *parent = nullptr,
                            bool wrongPassword = false);

    QString password() const;
    bool rememberPassword() const;

private:
    QLineEdit *m_passwordEdit = nullptr;
    QCheckBox *m_rememberCheck = nullptr;
};

#endif // PASSWORDDIALOG_H
