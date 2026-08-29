#ifndef OMANOTES_EDITOR_EDITOR_ADAPTER_HPP
#define OMANOTES_EDITOR_EDITOR_ADAPTER_HPP

#include <QObject>
#include <QString>

class QWidget;

namespace omanotes {

class EditorAdapter : public QObject {
    Q_OBJECT

  public:
    explicit EditorAdapter(QObject* parent = nullptr) : QObject(parent) {}
    ~EditorAdapter() override = default;

    [[nodiscard]] virtual QWidget* widget() noexcept = 0;
    [[nodiscard]] virtual QString text() const = 0;
    virtual void setText(const QString& text) = 0;
    [[nodiscard]] virtual bool isModified() const noexcept = 0;
    [[nodiscard]] virtual QString modeName() const = 0;

  signals:
    void modeChanged(const QString& modeName);
    void modifiedChanged(bool modified);
};

} // namespace omanotes

#endif // OMANOTES_EDITOR_EDITOR_ADAPTER_HPP
