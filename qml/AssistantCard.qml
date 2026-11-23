pragma ComponentBehavior: Bound

import QtQuick

// 管理后台的"AI 助手"卡片：选接入方式、填地址和模型、发一句话试试。
// 配置由 AssistantVM 存到本机，重启后还在。
Rectangle {
    id: card
    objectName: "assistantCard"

    property Theme theme

    readonly property bool useOllama: AssistantVM.provider === 1
    // 试发的结果只属于当前这套配置：切接入方式或重新保存就清掉。
    property string testText: ""
    property bool testOk: false

    function save() {
        card.testText = ""
        const ok = card.useOllama
            ? AssistantVM.saveOllamaConfig(ollamaUrl.text, ollamaModel.text)
            : AssistantVM.saveApiConfig(apiBaseUrl.text, apiKey.text, apiModel.text)
        if (ok)
            Notify.info("AI 接入设置已保存")
    }

    height: column.implicitHeight + theme.gap * 1.5
    radius: theme.radius
    color: theme.surface
    border.width: 2
    border.color: theme.line

    Connections {
        target: AssistantVM

        function onReplyReady(question, reply) {
            card.testOk = true
            card.testText = reply
        }

        function onFailed(message) {
            card.testOk = false
            card.testText = message
        }
    }

    Column {
        id: column
        width: parent.width - card.theme.gap * 2
        x: card.theme.gap
        y: card.theme.px(12)
        spacing: card.theme.gapSmall

        AppText {
            width: parent.width
            theme: card.theme
            role: "body"
            text: "陪聊用哪个大模型"
            wrapMode: Text.WordWrap
        }

        AppButton {
            width: parent.width
            height: card.theme.touch
            theme: card.theme
            text: "云端 API"
            kind: "secondary"
            selected: !card.useOllama
            onClicked: {
                card.testText = ""
                AssistantVM.provider = 0
            }
        }

        AppButton {
            width: parent.width
            height: card.theme.touch
            theme: card.theme
            text: "本地 Ollama"
            kind: "secondary"
            selected: card.useOllama
            onClicked: {
                card.testText = ""
                AssistantVM.provider = 1
            }
        }

        // ---- 云端：OpenAI 兼容接口 ----

        Field {
            id: apiBaseUrl
            objectName: "aiApiBaseUrl"
            width: parent.width
            visible: !card.useOllama
            theme: card.theme
            label: "接口地址"
            placeholder: "https://api.deepseek.com/v1"
            text: AssistantVM.apiBaseUrl
            fontSize: card.theme.px(20)
            maxLength: 200
            inputMethodHints: Qt.ImhUrlCharactersOnly
        }

        AppText {
            width: parent.width
            visible: !card.useOllama
            theme: card.theme
            role: "label"
            color: card.theme.inkSoft
            text: "填 OpenAI 兼容接口的地址，写到 /v1 为止，后面不要带 /chat/completions。"
            wrapMode: Text.Wrap
        }

        Field {
            id: apiKey
            width: parent.width
            visible: !card.useOllama
            theme: card.theme
            label: "密钥"
            placeholder: "sk-..."
            text: AssistantVM.apiKey
            fontSize: card.theme.px(20)
            maxLength: 200
            echoMode: TextInput.Password
            inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
        }

        Field {
            id: apiModel
            width: parent.width
            visible: !card.useOllama
            theme: card.theme
            label: "模型"
            placeholder: "deepseek-chat"
            text: AssistantVM.apiModel
            fontSize: card.theme.px(20)
            maxLength: 80
            inputMethodHints: Qt.ImhNoPredictiveText
        }

        // ---- 本地：局域网里的 Ollama ----

        Field {
            id: ollamaUrl
            width: parent.width
            visible: card.useOllama
            theme: card.theme
            label: "Ollama 地址"
            placeholder: "http://192.168.1.100:11434"
            text: AssistantVM.ollamaUrl
            fontSize: card.theme.px(20)
            maxLength: 200
            inputMethodHints: Qt.ImhUrlCharactersOnly
        }

        Field {
            id: ollamaModel
            width: parent.width
            visible: card.useOllama
            theme: card.theme
            label: "模型"
            placeholder: "qwen2.5:7b"
            text: AssistantVM.ollamaModel
            fontSize: card.theme.px(20)
            maxLength: 80
            inputMethodHints: Qt.ImhNoPredictiveText
        }

        AppText {
            objectName: "aiTestResult"
            width: parent.width
            visible: card.testText.length > 0 && !AssistantVM.busy
            theme: card.theme
            role: "label"
            color: card.testOk ? card.theme.primary : card.theme.danger
            text: (card.testOk ? "✓ 回复：" : "✗ ") + card.testText
            wrapMode: Text.Wrap
        }

        AppButton {
            objectName: "aiTest"
            width: parent.width
            height: card.theme.touch
            theme: card.theme
            text: AssistantVM.busy ? "正在等回复…" : "发一句话试试"
            kind: "secondary"
            enabled: !AssistantVM.busy && AssistantVM.configured
            onClicked: {
                card.testText = ""
                AssistantVM.ask("你好，请用一句话介绍你自己。")
            }
        }

        AppText {
            width: parent.width
            visible: !AssistantVM.configured
            theme: card.theme
            role: "label"
            color: card.theme.inkSoft
            text: "先把上面几项填好并保存，才能试发。"
            wrapMode: Text.WordWrap
        }

        AppButton {
            objectName: "aiSave"
            width: parent.width
            height: card.theme.touch
            theme: card.theme
            text: "保存 AI 设置"
            kind: "primary"
            onClicked: card.save()
        }
    }
}
