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
    let arr_message = message.split(",");
    let res_id = arr_message[0];
    let heart_rate = parseInt(arr_message[1]);
    let fall_detected = arr_message[2].substring(0,1);
    console.log("res_id: " + res_id);
    const resident = healthy_residents_container.querySelector("#id" + res_id);
    resident.querySelector(".heart-rate").textContent = "Heart Rate: " + heart_rate;
    if (fall_detected == 1) {
      resident.querySelector(".fall-detected").textContent = "Fall Detected!"
    }
    else {
      resident.querySelector(".fall-detected").textContent = "No fall detected";
    }

    if (fall_detected == "1" || heart_rate < heart_rate || heart_rate > 180) {
      // move to emergency
      const resident = healthy_residents_container.querySelector("#id" + res_id);
      emergency_residents_container.appendChild(resident);
      // healthy_residents_container.removeChild(emer_res);
    }
    
    const messageTextArea = document.querySelector("#message");
    messageTextArea.value += message.substring(0,6) + "\r\n";
  });
}

function subscribeToTopic() {
  const topic = "residents"
  // console.log(`Subscribing to Topic: ${topic}`);

  mqttClient.subscribe(topic, { qos: 0 });
}

