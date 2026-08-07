// The Flatpak-only QML typeinfo workaround suppresses Qt's generated
// registration source for this pure-QML module, but the generated static
// plugin still references this function. Keep this equivalent to the minimal
// source qmltyperegistrar normally emits for a module without C++ types.

#include <QtQml/qqml.h>
#include <QtQml/qqmlmoduleregistration.h>

#if !defined(QT_STATIC)
#define Q_QMLTYPE_EXPORT Q_DECL_EXPORT
#else
#define Q_QMLTYPE_EXPORT
#endif

Q_QMLTYPE_EXPORT void qml_register_types_Mixxx_Controls() {
    qmlRegisterModule("Mixxx.Controls", 1, 0);
}

static const QQmlModuleRegistration registration(
        "Mixxx.Controls", qml_register_types_Mixxx_Controls);
