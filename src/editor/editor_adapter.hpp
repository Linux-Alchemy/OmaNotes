#ifndef OMANOTES_EDITOR_EDITOR_ADAPTER_HPP
#define OMANOTES_EDITOR_EDITOR_ADAPTER_HPP

#include <QObject>
#include <QString>

#include <cstdint>

class QWidget;

namespace omanotes {

enum class EditorMode : std::uint8_t { Normal, Insert, Visual, Replace, Other };

struct EditorPosition {
    int line{0};
    int column{0};

    [[nodiscard]] bool operator==(const EditorPosition&) const = default;
};

class EditorAdapter : public QObject {
    Q_OBJECT

  public:
    explicit EditorAdapter(QObject* parent = nullptr) : QObject(parent) {}
    ~EditorAdapter() override = default;

    [[nodiscard]] virtual QWidget* widget() noexcept = 0;
    [[nodiscard]] virtual QString text() const = 0;
    virtual void setText(const QString& text) = 0;
    virtual void loadText(const QString& text) = 0;
    [[nodiscard]] virtual bool isModified() const noexcept = 0;
    /// Record that the document now matches what is on disk.
    virtual void markSaved() = 0;
    /// Mark the document as carrying unsaved edits, as a restored recovery
    /// record must: the text came from the record, not from the file.
    virtual void markModified() = 0;
    [[nodiscard]] virtual EditorPosition cursorPosition() const = 0;
    /// Move the cursor, clamped to the text that exists.
    virtual void setCursorPosition(EditorPosition position) = 0;
    [[nodiscard]] virtual int firstVisibleLine() const = 0;
    /// Scroll so `line` is the first visible line, clamped; the cursor stays.
    virtual void scrollToLine(int line) = 0;
    [[nodiscard]] virtual int lineCount() const = 0;
    [[nodiscard]] virtual EditorMode mode() const noexcept = 0;
    [[nodiscard]] virtual QString modeName() const = 0;

  signals:
    void modeChanged(const QString& modeName);
    void modifiedChanged(bool modified);
};

} // namespace omanotes

#endif // OMANOTES_EDITOR_EDITOR_ADAPTER_HPP
