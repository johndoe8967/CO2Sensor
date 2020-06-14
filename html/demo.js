// Called after form input is processed
function startConnect() {
	devicelist =[];
    // Generate a random client ID
    clientID = "clientID-" + parseInt(Math.random() * 100);

    // Fetch the hostname/IP address and port number from the form
    host = document.getElementById("host").value;
    port = document.getElementById("port").value;

    // Print output for the user in the messages div
    document.getElementById("messages").innerHTML += '<span>Connecting to: ' + host + ' on port: ' + port + '</span><br/>';
    document.getElementById("messages").innerHTML += '<span>Using the following client value: ' + clientID + '</span><br/>';

    // Initialize new Paho client connection
    client = new Paho.MQTT.Client(host, Number(port), clientID);

    // Set callback handlers
    client.onConnectionLost = onConnectionLost;
    client.onMessageArrived = onMessageArrived;

    // Connect the client, if successful, call onConnect function
    client.connect({
		userName: "iotMichael",
		password: "ot%d0$MRT12",
        onSuccess: onConnect,
    });
}

function rescan() {
	// Publish a Message
	var message = new Paho.MQTT.Message("scan");
	message.destinationName = "device";
	message.qos = 0;
	client.send(message);	
}

// Called when the client connects
function onConnect() {
    // Fetch the MQTT topic from the form
    topic = document.getElementById("topic").value;

    // Print output for the user in the messages div
    document.getElementById("messages").innerHTML += '<span>Subscribing to: ' + topic + '</span><br/>';

    // Subscribe to the requested topic
    client.subscribe(topic);
	client.subscribe("device/scan");
	// Publish a Message
	var message = new Paho.MQTT.Message("scan");
	message.destinationName = "device";
	message.qos = 0;
	client.send(message);
}

// Called when the client loses its connection
function onConnectionLost(responseObject) {
    document.getElementById("messages").innerHTML += '<span>ERROR: Connection lost</span><br/>';
    if (responseObject.errorCode !== 0) {
        document.getElementById("messages").innerHTML += '<span>ERROR: ' + responseObject.errorMessage + '</span><br/>';
    }
}

// Called when a message arrives
function onMessageArrived(message) {
	var device = message.destinationName.substr(0,message.destinationName.indexOf("/"));
//	console.log("onMessageArrived: " + message.payloadString);
	if (message.destinationName == "device/scan") {
		document.getElementById("messages").innerHTML += '<span>Topic: ' + message.destinationName + '  | ' + message.payloadString + '</span><br/>';		
		console.log(devicelist);
		if (devicelist.includes(message.payloadString)) {
			console.log("already scanned device" + message.payloadString);
		} else {
			console.log("add new device" + message.payloadString);
			devicelist.push(message.payloadString);
			client.subscribe(message.payloadString + "/commands");
			var getCommands = new Paho.MQTT.Message("getCommands");
			getCommands.destinationName = message.payloadString;
			getCommands.qos = 0;
			client.send(getCommands);
		}
	} else if (devicelist.includes(device)) {
		console.log("add commands");
		document.getElementById("messages").innerHTML += '<span>Topic: ' + message.destinationName + '  | ' + message.payloadString + '</span><br/>';
		var cmds = JSON.parse(message.payloadString);
		for (var cmd of cmds.commands) {
			console.log(cmd);
			
			if (cmd.type == "bool") {
				document.getElementById("devices").innerHTML += '<input id="'+cmd.cmd+'" type="checkbox" name="'+device+'.'+cmd.cmd+'" onchange="sendParameter(this)" value="0">';				
				document.getElementById("devices").innerHTML += '<label for="'+cmd.cmd+'">'+cmd.cmd+'</label><br><br>';
			} else {
				document.getElementById("devices").innerHTML += '<span>' + cmd.cmd + '</span><br/>';
				document.getElementById("devices").innerHTML += '<input id="'+cmd.cmd+'" type="text" name="'+device+'.'+cmd.cmd+'" onchange="sendParameter(this)" value="0"><br><br>';
			}
		}
	}
	else {
		document.getElementById("messages").innerHTML += '<span>Topic: ' + message.destinationName + '  | ' + message.payloadString + '</span><br/>';
		updateScroll();		
	}
}

function sendParameter(input) {
	var parameter ={};
	var parameterName = input.name.substring(input.name.indexOf(".")+1,input.name.length);

	var value;
	if (input.type == "checkbox") {
		value = input.checked;
		parameter[parameterName] = value;
	} else {
		value = input.value;
		parameter[parameterName] = parseInt(value);
	}
	console.log("now send"+input.name+" "+value);
	var destination = input.name.substring(0,input.name.indexOf("."));
	var myMessagepayload = JSON.stringify(parameter);
	var sendUpdate = new Paho.MQTT.Message(myMessagepayload);
	sendUpdate.destinationName = destination;
	sendUpdate.qos = 0;
	client.send(sendUpdate);
}

// Updates #messages div to auto-scroll
function updateScroll() {
    var element = document.getElementById("messages");
    element.scrollTop = element.scrollHeight;
}

// Called when the disconnection button is pressed
function startDisconnect() {
    client.disconnect();
    document.getElementById("messages").innerHTML += '<span>Disconnected</span><br/>';
}