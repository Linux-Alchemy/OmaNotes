#ifndef OMANOTES_EDITOR_KTEXT_EDITOR_ADAPTER_HPP
#define OMANOTES_EDITOR_KTEXT_EDITOR_ADAPTER_HPP

#include "editor/editor_adapter.hpp"

namespace KTextEditor {
class Document;
class View;
} // namespace KTextEditor

namespace omanotes {

class KTextEditorAdapter final : public EditorAdapter {
    Q_OBJECT

  public:
    explicit KTextEditorAdapter(QWidget* viewParent, QObject* parent = nullptr);
    ~KTextEditorAdapter() override;

    [[nodiscard]] QWidget* widget() noexcept override;
    [[nodiscard]] QString text() const override;
    void setText(const QString& text) override;
    void loadText(const LoadedText& document) override;
    [[nodiscard]] bool isModified() const noexcept override;
    [[nodiscard]] EditorMode mode() const noexcept override;
    [[nodiscard]] QString modeName() const override;

  private:
    KTextEditor::Document* document_;
    KTextEditor::View* view_;
};

} // namespace omanotes

#endif // OMANOTES_EDITOR_KTEXT_EDITOR_ADAPTER_HPP
