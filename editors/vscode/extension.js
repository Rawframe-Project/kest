// The client, which is all this extension is: every semantic answer comes from
// `kest lsp`, which is the compiler. Nothing here parses Kest, because a second
// parser in an editor is a second answer about what a file means. See D978.

const {
    commands,
    debug,
    DebugAdapterExecutable,
    window,
    workspace,
} = require("vscode");
const { LanguageClient, TransportKind } = require("vscode-languageclient/node");

let client;

// The debugger is `kest dap`, the same debugger `kest debug` is, answering the
// Debug Adapter Protocol: breakpoints set in the gutter, the frames and what
// each body called its slots in the side bar, and the program's own writing on
// the debug console. This starts it on the file in front of the person. See
// D1182.
function debugThisFile() {
    const editor = window.activeTextEditor;
    if (!editor || editor.document.languageId !== "kest") {
        window.showInformationMessage("open a `.kest` file to debug it");
        return;
    }
    debug.startDebugging(undefined, {
        type: "kest",
        request: "launch",
        name: "Kest: this file",
        program: editor.document.fileName,
    });
}

// What VS Code runs when a `kest` session starts: the command the setting
// names, asked to be an adapter.
const adapters = {
    createDebugAdapterDescriptor() {
        const command =
            workspace.getConfiguration("kest").get("path") || "kest";
        return new DebugAdapterExecutable(command, ["dap"]);
    },
};

function activate(context) {
    const command = workspace.getConfiguration("kest").get("path") || "kest";
    const server = {
        command,
        args: ["lsp"],
        transport: TransportKind.stdio,
    };
    client = new LanguageClient(
        "kest",
        "Kest",
        { run: server, debug: server },
        {
            documentSelector: [{ scheme: "file", language: "kest" }],
            synchronize: {
                fileEvents: workspace.createFileSystemWatcher("**/*.kest"),
            },
        }
    );
    context.subscriptions.push(
        commands.registerCommand("kest.debug", debugThisFile),
        debug.registerDebugAdapterDescriptorFactory("kest", adapters)
    );
    return client.start();
}

function deactivate() {
    return client ? client.stop() : undefined;
}

module.exports = { activate, deactivate };
