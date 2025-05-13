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
        <script src="https://cdn.jsdelivr.net/gh/alpinejs/alpine@v2.3.5/dist/alpine.min.js" defer></script>
        <script src="https://cdnjs.cloudflare.com/ajax/libs/ace/1.4.12/ace.js" type="text/javascript" charset="utf-8" crossorigin="anonymous"></script>
        <link rel="stylesheet" href="style.css" />
        <style type="text/css" media="screen">
            
        </style>
    </head>
    <body>
        <div x-data="shaders()" x-init="initialize()" class="app">
            <h1>Select mode</h1>
            <ul>
                <template x-for="shader in shaders" :key="shader.name">
                <li :class="{'completed': shader.selected}">
                    <span @click="selectShader(shader)" x-text="shader.name" class="title"></span>
                    <span @click="deleteShader(shader.name)" class="delete-shader">&times;</span>
                    <span @click="editShader(shader.name)" class="edit-shader">&equiv;</span>
                </li>
                </template>
            </ul>
            <div class="add-shader">
                <input type="text" x-model="inputValue" placeholder="NewShader" />
                <button @click="addShader()">Upload</button>
            </div>
            <br/>
            <div>
                <div id="editor" :class="{'hidden': !inputValue}">-- example function of hsv rainbow
function draw(led_count)
    for i = 0, led_count - 1 do
        hsv(i, env.millis / 10 + i * 5, 255, 255)
    end
end
</div>
            </div>
        </div>

        <script>
            var serverIp = "%SELF_IP%"
            if (serverIp.includes("SELF_IP")) {
                serverIp = "10.0.0.7"
            }
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
                    selectShader: function(s) {
                        if (s === this.currentShader) {
                            return;
                        }
                        // this.currentShader.selected = false;
                        // this.currentShader = s;
                        // this.currentShader.selected = true;
                        this.websocket.send(`select ${s.name}`);
                    },
                    updateSelectedByName: function(name) {
                        const shader = this.shaders.find(s => s.name === name)
                        if (this.currentShader != null) {
                            this.currentShader.selected = false;
                        }
                        this.currentShader = shader;
                        this.currentShader.selected = true;
                    },
                    addShader: function() {
                        if (!this.inputValue) {
                            return;
                        }
                        const data = { name: this.inputValue, shader: editor.getValue() };
                        console.log(JSON.stringify(data));

                        fetch(`http://${serverIp}/api/shader`, {
                            method: 'POST',
                            headers: { 'Content-Type': 'application/json' },
                            body: JSON.stringify(data),
                        })
                        .then(response => response)
                        .then(response => {
                            this.inputValue = "";
                        });
                    },
                    deleteShader: function(id) {
                        fetch(`http://${serverIp}/api/shader/${id}`, {method: 'DELETE'});
                    },
                    initWebSocket: function() {
                        console.log('Trying to open a WebSocket connection...');
                        this.websocket = new WebSocket(gateway);
                        this.websocket.onopen = function(event) {
                            console.log('Connection opened');
                        };
                        this.websocket.onclose = function(event) {
                            console.log('Connection closed');
                            setTimeout(this.initWebSocket, 2000);
                        }.bind(this);
                        this.websocket.onmessage = this.handleMessage.bind(this);
                    },
                    handleMessage: function(event) {
                        console.log(event.data);
                        const command = event.data;
                        if (command.startsWith("select ")) {
                            this.updateSelectedByName(command.substring(7));
                        }
                        if (command.startsWith("add ")) {
                            if (!this.shaders.find(s => s.name === command.substring(4)))
                                this.shaders.push({ name: command.substring(4), selected: false });
                        }
                        if (command.startsWith("delete ")) {
                            this.shaders = this.shaders.filter(shader => shader.name !== command.substring(7));
                        }
                    },
                    initialize: function() {
                        this.initWebSocket();

                        fetch(`http://${serverIp}/api/shader`)
                        .then(response => response.json())
                        .then(data => {
                            this.shaders = data.shader.map(s => { return { name: s, selected: false } });

                            fetch(`http://${serverIp}/api/show`)
                            .then(response => response.json())
                            .then(data => {
                                this.updateSelectedByName(data.name);
                            });
                        });
                    },
                    editShader: function(name) {
                        fetch(`http://${serverIp}/api/shader/${name}`)
                        .then(response => response.json())
                        .then(data => {
                            this.inputValue = name;
                            editor.setValue(data.shader);
                        });
                    }
                };
            }
        </script>
    </body>
</html>

)rawliteral";

const char style_css[] PROGMEM = R"rawliteral(
body {
    font-family: "Segoe UI", Tahoma, Geneva, Verdana, sans-serif;
    display: flex;
    flex-direction: column;
    align-items: center;
    margin-top: 1rem;
  }
  
  .app {
    min-width: 300px;
    max-width: 700px;
    border-radius: 2px;
    padding: 20px;
    font-size: 1.125rem;
    box-shadow: 0 10px 20px rgba(0, 0, 0, 0.19), 0 6px 6px rgba(0, 0, 0, 0.23);
  }
  
  h1 {
    font-size: 1.5rem;
  }
  
  ul {
    margin: 1rem 0;
    padding: 0;
  }
  
  li {
    list-style: none;
    cursor: pointer;
    padding: 0.5rem;
    border-radius: 2px;
    transition: all 0.3s cubic-bezier(0.25, 0.8, 0.25, 1);
  }
  
  li:hover {
    box-shadow: 0 3px 6px rgba(0, 0, 0, 0.16), 0 3px 6px rgba(0, 0, 0, 0.23);
  }
  
  ul li::before {
    content: "◻";
    display: inline-block;
    margin-right: 0.2rem;
  }
  
  .add-shader {
    display: flex;
  }
  
  .add-shader input {
    flex: 1;
    padding: 0.4rem;
    padding-left: 0.5rem;
  }
  
  .add-shader button {
    margin-left: 0.5rem;
    padding: 0 10px;
    background: #3182ce;
    color: white;
    border: none;
    transition: all 0.3s;
  }
  
  .add-shader button:hover {
    background: #2a4365;
    transition: all 0.3s;
  }
  
  .title {
    margin-left: 10px;
    margin-right: 10px;
  }

  .edit-shader {
    color: rgb(170, 170, 0);
    font-weight: bold;
    font-size: 1.7rem;
    float: right;
  }
  
  .delete-shader {
    color: red;
    font-weight: bold;
    font-size: 1.5rem;
    float: right;
  }
  
  .completed .title {
    /* text-decoration: line-through; */
  }

  .hidden {
    display: none;
  }
  
  ul li.completed::before {
    content: "✔";
  }
  
  *,
  *::before,
  *::after {
    box-sizing: border-box;
  }
  
  ul[class],
  ol[class] {
    padding: 0;
  }
  
  body,
  h1,
  h2,
  h3,
  h4,
  p,
  ul[class],
  ol[class],
  li,
  figure,
  figcaption,
  blockquote,
  dl,
  dd {
    margin: 0;
  }
  
  body {
    /* min-height: 100vh; */
    scroll-behavior: smooth;
    text-rendering: optimizeSpeed;
    line-height: 1.5;
  }
  
  ul[class],
  ol[class] {
    list-style: none;
  }
  
  a:not([class]) {
    text-decoration-skip-ink: auto;
  }
  
  img {
    max-width: 100%;
    display: block;
  }
  
  article > * + * {
    margin-top: 1em;
  }
  
  input,
  button,
  textarea,
  select {
    font: inherit;
  }
  
  @media (prefers-reduced-motion: reduce) {
    * {
      animation-duration: 0.01ms !important;
      animation-iteration-count: 1 !important;
      transition-duration: 0.01ms !important;
      scroll-behavior: auto !important;
    }
  }

  #editor {
    height: 400px;
    width: 600px;
    font-size: 16px;
}

)rawliteral";

#endif //W_MAIN_H