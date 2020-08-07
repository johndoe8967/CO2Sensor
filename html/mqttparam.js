// initiate mqtt connection, called by connect button
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

// rescan all devices, called by rescan button
function rescan() {
	// Publish scan message to all devices 
	var message = new Paho.MQTT.Message("scan");
	message.destinationName = "device";
	message.qos = 0;
	client.send(message);	
}

// Called when the client connects to the mqtt broker
function onConnect() {
    // Fetch the MQTT topic from the form
    topic = document.getElementById("topic").value;

    // Print output for the user in the messages div
    document.getElementById("messages").innerHTML += '<span>Subscribing to: ' + topic + '</span><br/>';

    // Subscribe to the requested topic
    client.subscribe(topic);
	
	client.subscribe("device/scan");	
	rescan();
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
	//guess device from topic. Format <device>/<command>
	var device = message.destinationName.substr(0,message.destinationName.indexOf("/"));
	var command = message.destinationName.substr(message.destinationName.indexOf("/"),message.destinationName.length)
	
	//message is a scan response from clients on "device/scan"?
	if (message.destinationName == "device/scan") {
		//debug output on site
		document.getElementById("messages").innerHTML += '<span>Topic: ' + message.destinationName + '  | ' + message.payloadString + '</span><br/>';		
		
		//get device name from response
		var deviceName = message.payloadString;
		//check if device is already known
		if (!devicelist.includes(deviceName)) {
			//create new device on site
			document.getElementById("devices").innerHTML += '<span>' + deviceName + '</span><br/>';
			document.getElementById("devices").innerHTML += '<div id="'+deviceName+'"></div><br/>';
			var element = document.getElementById(deviceName);
			element.classList.add("devices");

			//add device to list of known devices
			devicelist.push(deviceName);
			
			// subscribe commands from new device
			client.subscribe(deviceName + "/commands");
			
			// initiate getCommand on new device
			if (document.getElementById("stayOnline").checked) {
				var sendStayOnline = new Paho.MQTT.Message("{\"Debug\":true}");
				sendStayOnline.destinationName = deviceName;
				sendStayOnline.qos = 0;
				client.send(sendStayOnline);				
			}
						
			// initiate getCommand on new device
			var getCommands = new Paho.MQTT.Message("getCommands");
			getCommands.destinationName = deviceName;
			getCommands.qos = 0;
			client.send(getCommands);
			
		} else {
			console.log("already scanned device" + message.payloadString);
		}
	} else 
		//message is a response on a "getCommand" from a known device
		if (command = "commands" && devicelist.includes(device)) {
		//debug output on site
		document.getElementById("messages").innerHTML += '<span>Topic: ' + message.destinationName + '  | ' + message.payloadString + '</span><br/>';
		
		//parse json response for valid commands
		var cmds = JSON.parse(message.payloadString);
		//iterate on commands
		for (var cmd of cmds.commands) {			
			//type specific behaviour of command
			switch (cmd.type) {
				case "bool":
					document.getElementById(device).innerHTML += '<b>' + cmd.cmd + '</b>';
					document.getElementById(device).innerHTML += '<input id="'+cmd.cmd+'" type="checkbox" name="'+device+'.'+cmd.cmd+'" onchange="sendParameter(this)" value="0"><br/>';				
					break;
				case "integer":
					document.getElementById(device).innerHTML += '<b>' + cmd.cmd + '</b>';
					document.getElementById(device).innerHTML += '<input id="'+cmd.cmd+'" type="text" name="'+device+'.'+cmd.cmd+'" onchange="sendParameter(this)" value="'+cmd.value+'"><br/>';
					break;
			}
		}
	}
	else {
		document.getElementById("messages").innerHTML += '<span>Topic: ' + message.destinationName + '  | ' + message.payloadString + '</span><br/>';
		updateScroll();		
	}
}

//send parameter to device input has to be a valid input item on site
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