/**
 * AN-24 Blueprint Editor
 * Main entry point - delegates to EditorApp
 */

#include "editor/app/editor_app.h"

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    return EditorApp().run();
}
