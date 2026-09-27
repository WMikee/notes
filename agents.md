# AGENT INSTRUCTIONS FOR QT DEVELOPMENT

When generating, modifying, or debugging Qt code, you MUST adhere to the following rules:

1. **Target Qt 6:** Assume all Qt development is for Qt 6 unless specified otherwise. Do not use deprecated Qt 5 classes, modules, or CMake macros (e.g., avoid `QRegExp`, use `QRegularExpression`; be mindful of module shifts).
2. **Consult Official Docs First:** Before making assumptions about class methods, signals, properties, or CMake integration, query the official Qt 6 documentation: 
**https://doc.qt.io/qt-6/**
3. **Use Modern C++ & CMake:** Prefer C++17/C++20 standards. Ensure build instructions and code snippets utilize modern Qt 6 CMake commands (e.g., `qt_add_executable`, `qt_add_qml_module`).
4. **No Hallucinations:** If you are unsure about a specific API signature or include path in Qt 6, state your uncertainty or perform a web search against `site:doc.qt.io/qt-6/` before providing a solution.