// The client, which is all this extension is: every semantic answer comes from
// `kest lsp`, which is the compiler. Nothing here parses Kest, because a second
// parser in an editor is a second answer about what a file means. See D978.

const { workspace } = require("vscode");
const { LanguageClient, TransportKind } = require("vscode-languageclient/node");

let client;

function activate() {
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
    return client.start();
}

function deactivate() {
    return client ? client.stop() : undefined;
}

module.exports = { activate, deactivate };
