let mqttClient;

window.addEventListener("load", (event) => {
  connectToBroker();

  // const subscribeBtn = document.querySelector("#subscribe");
  // subscribeBtn.addEventListener("click", function () {
  //   subscribeToTopic();
  // });
  subscribeToTopic();

  // const unsubscribeBtn = document.querySelector("#unsubscribe");
  // unsubscribeBtn.addEventListener("click", function () {
  //   unsubscribeToTopic();
  // });

  const clickButton = document.querySelector("button");
  clickButton.addEventListener("click", function () {
    console.log("yo");
    buttonClick();
  });
});

function connectToBroker() {
  const clientId = "client" + Math.random().toString(36).substring(7);

  // Change this to point to your MQTT broker
  const host = "ws://mqtt.eclipseprojects.io:80/mqtt";

  const options = {
    keepalive: 60,
    clientId: clientId,
    protocolId: "MQTT",
    protocolVersion: 4,
    clean: true,
    reconnectPeriod: 1000,
    connectTimeout: 30 * 1000,
  };

  mqttClient = mqtt.connect(host, options);

  mqttClient.on("error", (err) => {
    console.log("Error: ", err);
    mqttClient.end();
  });

  mqttClient.on("reconnect", () => {
    console.log("Reconnecting...");
  });

  mqttClient.on("connect", () => {
    console.log("Client connected:" + clientId);
  });

  // Received
  mqttClient.on("message", (topic, message, packet) => {
    console.log(
      "Received Message: " + message.toString() + "\nOn topic: " + topic
    );

    const emergency_residents_container = document.querySelector(".emergency-residents");
    const healthy_residents_container = document.querySelector(".healthy-residents");

    // Parse message
    message = message + " ";
    let arr_message = message.split(", ");
    let name = arr_message[0];
    let id = arr_message[1];
    let heart_rate = parseInt(arr_message[2]);
    let fall_detected = arr_message[3];
    console.log("name:${name}, id:${id}, hr:${heart_rate}, fall:${fall_detected}");

    if (fall_detected == "1" || heart_rate < heart_rate || heart_rate > 180) {
      // move to emergency
      const emer_res = healthy_residents_container.querySelector("#id${id}");
      emergency_residents_container.appendChild(emer_res);
      // healthy_residents_container.removeChild(emer_res);
    }
    
    const messageTextArea = document.querySelector("#message");
    messageTextArea.value += message + "\r\n";
  });
}

function subscribeToTopic() {
  const topic = "residents"
  // console.log(`Subscribing to Topic: ${topic}`);

  mqttClient.subscribe(topic, { qos: 0 });
}

function buttonClick() {
  const emergency_residents_container = document.querySelector(".emergency-residents");
  const healthy_residents_container = document.querySelector(".healthy-residents");
  console.log("yo");
  const emer_res = healthy_residents_container.querySelector("#id1");
  emergency_residents_container.appendChild(emer_res);
}