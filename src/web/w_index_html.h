#ifndef W_MAIN_H
#define W_MAIN_H

#include<Arduino.h>

const char index_html[] PROGMEM = R"rawliteral(

<!DOCTYPE html>
<html lang="en">

<head>
    <meta charset="UTF-8" />
    <meta name="viewport" content="width=device-width, initial-scale=1.0" />
    <title>LED</title>
    <script src="https://cdn.tailwindcss.com"></script>
    <link href="https://cdn.jsdelivr.net/npm/daisyui@3.9.2/dist/full.css" rel="stylesheet" type="text/css" />
    <script src="https://cdn.jsdelivr.net/gh/alpinejs/alpine@v2.3.5/dist/alpine.min.js" defer></script>
    <script src="https://cdnjs.cloudflare.com/ajax/libs/ace/1.4.12/ace.js" crossorigin="anonymous"></script>
</head>

<body class="flex flex-col items-center mt-4 font-sans text-base">
    <div class="flex flex-wrap gap-1 justify-center mb-4 rounded-full" id="ledPreview"></div>
    <div class="fixed top-4 right-4 p-2 max-h-[90vh] overflow-y-auto text-xs w-28">
        <div class="font-bold mb-2 text-center">HSV Hue</div>
        <template id="hsvHueTemplate">
            <!-- JS will populate this -->
        </template>
    </div>
    <div x-data="shaders()" x-init="initialize()" class="w-full max-w-xl p-4 card bg-base-100 shadow-lg">
        <div class="flex flex-row items-center gap-4">
            <p class="text-m">Led limit</p>
            <input type="number" min="0" max="200" x-model.number="ledLimit"
                @keyup.enter="websocket && websocket.send(`limitLeds ${ledLimit}`)" class="input input-bordered" />
        </div>
        <h1 class="text-xl font-bold my-4">Select mode</h1>

        <ul class="flex flex-col gap-2 mb-4">
            <template x-for="shader in shaders" :key="shader.name">
                <li :class="{'bg-base-200': shader.selected}"
                    class="p-3 rounded border border-base-300 flex justify-between items-center hover:shadow transition cursor-pointer">
                    <div class="flex items-center gap-2">
                        <span x-text="shader.selected ? '✔' : '◻'"></span>
                        <span @click="selectShader(shader)" x-text="shader.name" class="font-semibold"></span>
                    </div>
                    <div class="flex gap-2">
                        <button @click="editShader(shader.name)" class="btn btn-xs btn-warning">Edit</button>
                        <button @click="deleteShader(shader.name)" class="btn btn-xs btn-error">Delete</button>
                    </div>
                </li>
            </template>
        </ul>

        <div class="form-control mb-4">
            <div class="input-group">
                <input type="text" x-model="inputValue" placeholder="NewShader" class="input input-bordered w-full" />
                <button @click="addShader()" class="btn btn-primary">Upload</button>
            </div>
        </div>

        <div>
            <div id="editor" :class="{'hidden': !inputValue}" class="h-[400px] w-full text-sm border rounded">-- example function of hsv rainbow
function draw(led_count)
    for i = 0, led_count - 1 do
        hsv(i, env.millis / 10 + i * 5, 255, 255)
    end
end
            </div>
        </div>
    </div>

    <script>
        var serverIp = "%SELF_IP%";
        if (serverIp.includes("SELF_IP")) serverIp = "led.local";
        var gateway = `ws://${serverIp}/control`;

        var editor = ace.edit("editor");
        editor.setTheme("ace/theme/monokai");
        editor.session.setMode("ace/mode/lua");

        function shaders() {
            return {
                shaders: [],
                currentShader: null,
                inputValue: "",
                websocket: null,
                ledLimit: null,
                selectShader(s) {
                    if (s === this.currentShader) return;
                    this.websocket.send(`select ${s.name}`);
                },
                updateSelectedByName(name) {
                    const shader = this.shaders.find(s => s.name === name);
                    if (this.currentShader) this.currentShader.selected = false;
                    this.currentShader = shader;
                    if (shader) shader.selected = true;
                },
                addShader() {
                    if (!this.inputValue) return;
                    const data = { name: this.inputValue, shader: editor.getValue() };
                    fetch(`http://${serverIp}/api/shader`, {
                        method: "POST",
                        headers: { "Content-Type": "application/json" },
                        body: JSON.stringify(data),
                    }).then(() => (this.inputValue = ""));
                },
                deleteShader(id) {
                    fetch(`http://${serverIp}/api/shader/${id}`, { method: "DELETE" });
                },
                initWebSocket() {
                    this.websocket = new WebSocket(gateway);
                    this.websocket.onopen = () => console.log("Connection opened");
                    this.websocket.onclose = () => {
                        console.log("Connection closed");
                        setTimeout(this.initWebSocket, 2000);
                    };
                    this.websocket.onmessage = this.handleMessage.bind(this);
                },
                handleTextMessage(event) {
                    const cmd = event.data;
                    if (cmd.startsWith("select ")) this.updateSelectedByName(cmd.substring(7));
                    if (cmd.startsWith("add ")) {
                        const name = cmd.substring(4);
                        if (!this.shaders.find(s => s.name === name)) this.shaders.push({ name, selected: false });
                    }
                    if (cmd.startsWith("delete ")) {
                        const name = cmd.substring(7);
                        this.shaders = this.shaders.filter(s => s.name !== name);
                    }
                },
                async handleMessage(e) {
                    const buffer = await e.data.arrayBuffer();
                    const data = new Uint8Array(buffer);
                    const type = data[0];
                    if (type === 0x00) {
                        const text = new TextDecoder().decode(data.slice(1));
                        this.handleTextMessage({ data: text });
                    } else if (type === 0x01) {
                        const ledCount = data[1];
                        const container = document.getElementById("ledPreview");
                        container.innerHTML = "";
                        let size = 20;
                        const maxWidth = container.offsetWidth || window.innerWidth;
                        const estTotalWidth = ledCount * (size + 4);
                        if (estTotalWidth > maxWidth) size = 10;
                        for (let i = 0; i < ledCount; i++) {
                            const r = data[2 + i * 3];
                            const g = data[3 + i * 3];
                            const b = data[4 + i * 3];
                            const el = document.createElement("div");
                            el.style.width = `${size}px`;
                            el.style.height = `${size}px`;
                            el.className = "rounded-full";
                            el.style.backgroundColor = `rgb(${r},${g},${b})`;
                            container.appendChild(el);
                        }
                    }
                },
                initialize() {
                    function createHueCheatsheet() {
                        const container = document.querySelector("template#hsvHueTemplate").parentElement;
                        for (let h = 0; h <= 255; h += 20) {
                            const div = document.createElement("div");
                            div.className = "flex items-center gap-1 mb-1";
                            div.innerHTML = `
                                <div style="background-color: hsl(${(h / 256) * 360} 100 50)" class="w-3 h-3 rounded-sm"></div>
                                <span>${h}</span>
                            `;
                            container.appendChild(div);
                        }
                    }
                    createHueCheatsheet();

                    this.initWebSocket();
                    fetch(`http://${serverIp}/api/shader`)
                        .then(r => r.json())
                        .then(data => {
                            this.shaders = data.shader.map(name => ({ name, selected: false }));
                            fetch(`http://${serverIp}/api/show`)
                                .then(r => r.json())
                                .then(data => {
                                    this.updateSelectedByName(data.name);
                                    this.ledLimit = data.ledLimit;
                                });
                        });
                },
                editShader(name) {
                    fetch(`http://${serverIp}/api/shader/${name}`)
                        .then(r => r.json())
                        .then(data => {
                            this.inputValue = name;
                            editor.setValue(data.shader || "");
                        });
                },
            };
        }
    </script>
</body>

</html>

)rawliteral";

#endif //W_MAIN_H