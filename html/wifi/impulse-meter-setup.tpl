<html><head><title>WiFi connection</title>
<link rel="stylesheet" type="text/css" href="style.css">
<script type="text/javascript" src="140medley.min.js"></script>
<script type="text/javascript">

var xhr=j();
var currAp="%currSsid%";

function createInputForAp(ap) {
	if (ap.essid=="" && ap.rssi==0) return;
	var div=document.createElement("div");
	div.id="apdiv";
	var rssi=document.createElement("div");
	var rssiVal=-Math.floor(ap.rssi/51)*32;
	rssi.className="icon";
	rssi.style.backgroundPosition="0px "+rssiVal+"px";
	var encrypt=document.createElement("div");
	var encVal="-64"; //assume wpa/wpa2
	if (ap.enc=="0") encVal="0"; //open
	if (ap.enc=="1") encVal="-32"; //wep
	encrypt.className="icon";
	encrypt.style.backgroundPosition="-32px "+encVal+"px";
	var input=document.createElement("input");
	input.type="radio";
	input.name="essid";
	input.value=ap.essid;
	if (currAp==ap.essid) input.checked="1";
	input.id="opt-"+ap.essid;
	var label=document.createElement("label");
	label.htmlFor="opt-"+ap.essid;
	label.textContent=ap.essid;
	div.appendChild(input);
	div.appendChild(rssi);
	div.appendChild(encrypt);
	div.appendChild(label);
	return div;
}

function getSelectedEssid() {
	var e=document.forms.wifiform.elements;
	for (var i=0; i<e.length; i++) {
		if (e[i].type=="radio" && e[i].checked) return e[i].value;
	}
	return currAp;
}


function scanAPs() {
	xhr.open("GET", "wifiscan.cgi");
	xhr.onreadystatechange=function() {
		if (xhr.readyState==4 && xhr.status>=200 && xhr.status<300) {
			var data=JSON.parse(xhr.responseText);
			currAp=getSelectedEssid();
			if (data.result.inProgress=="0" && data.result.APs.length>1) {
				$("#aps").innerHTML="";
				for (var i=0; i<data.result.APs.length; i++) {
					if (data.result.APs[i].essid=="" && data.result.APs[i].rssi==0) continue;
					$("#aps").appendChild(createInputForAp(data.result.APs[i]));
				}
				window.setTimeout(scanAPs, 20000);
			} else {
				window.setTimeout(scanAPs, 1000);
			}
		}
	}
	xhr.send();
}


window.onload=function(e) {
	scanAPs();
};
</script>
</head>
<body>
<div id="main">
<form name="wifiform" action="setup.cgi" method="post">
<p>
<table border="0" cellspacing="10">
  <tr align="left" valign="top">
    <td rowspan="4" align="left" valign="top"><div id="aps">Scanning...</div><br><br><br>WiFi password, if applicable: <br>
    <input type="text" name="passwd" value="%WiFiPasswd%"></td>
    <td>&nbsp;</td>
    <td align="left" valign="top">Meter serial (must be 7 digits): <br />
    <input type="text" name="impulse_meter_serial" value="%ImpulseMeterSerial%" maxlength="12"></td>
  </tr>
  <tr>
    <td>&nbsp;</td>
    <td align="left" valign="top">Meter energy in kWh: <br />
    <input type="text" name="impulse_meter_energy" value="%ImpulseMeterEnergy%" maxlength="16"></td>
  </tr>
  <tr>
    <td>&nbsp;</td>
    <td align="left" valign="top">Impulses pr. kWh: <br />
    <input type="text" name="impulses_per_kwh" value="%ImpulsesPerKwh%" maxlength="16"></td>
  </tr>
  <tr>
    <td>&nbsp;</td>
    <td align="left" valign="top">&nbsp;</td>
  </tr>
  <tr>
   <td align="left" valign="top">&nbsp;</td>
   <td>&nbsp;</td>
   <td align="left" valign="top"><br />
    <input type="submit" name="connect" value="Setup"></td>
 </tr>
</table></p>
</div>
</body>
</html>
