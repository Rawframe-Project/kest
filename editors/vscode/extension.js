// The client, which is all this extension is: every semantic answer comes from
// `kest lsp`, which is the compiler. Nothing here parses Kest, because a second
// parser in an editor is a second answer about what a file means. See D978.

const { commands, window, workspace } = require("vscode");
const { LanguageClient, TransportKind } = require("vscode-languageclient/node");

let client;

// The debugger is a command line rather than a debug protocol -- one surface
// finished beats two started, and D991 says why -- so what an editor offers is
// the command line in a terminal, on the file in front of the person. When
// there is a protocol this becomes a debug adapter and nothing else here
// changes. See D978.
function debugThisFile() {
    const editor = window.activeTextEditor;
    if (!editor || editor.document.languageId !== "kest") {
        window.showInformationMessage("open a `.kest` file to debug it");
        return;
    }
    const command = workspace.getConfiguration("kest").get("path") || "kest";
    const terminal = window.createTerminal("kest debug");
    terminal.show();
    terminal.sendText(`${command} debug ${editor.document.fileName}`);
}

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
        commands.registerCommand("kest.debug", debugThisFile)
    );
    return client.start();
}

function deactivate() {
    return client ? client.stop() : undefined;
}

module.exports = { activate, deactivate };
