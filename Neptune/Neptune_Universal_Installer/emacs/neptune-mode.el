;;; neptune-mode.el
(define-derived-mode neptune-mode prog-mode "Neptune"
  "Major mode for Neptune C--."
  (setq-local comment-start "#")
  (setq-local font-lock-defaults
    '((("\\<\\(var\\|vartable\\|struct\\)\\>" . font-lock-type-face)
       ("\\<\\(if\\|else\\|while\\|for\\|end\\|do\\|return\\|include\\)\\>" . font-lock-keyword-face)
       ("\\<\\(efunc\\|echo\\|cin\\)\\>" . font-lock-function-name-face)
       ("\\<\\(true\\|false\\)\\>" . font-lock-constant-face)
       ("\\<[0-9]+\\(\\.[0-9]+\\)?\\>" . font-lock-constant-face)
       ("\"[^\"\\n]*\"" . font-lock-string-face)
       ("#.*$" . font-lock-comment-face)))))
(add-to-list 'auto-mode-alist '("\\.nep\\'" . neptune-mode))
(provide 'neptune-mode)
