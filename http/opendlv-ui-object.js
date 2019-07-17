
let ctx;
let canvas;
let map;
let markers;
let changedView = false;
let currentFrameId;
let currentLocalObjects = {};


window.onload = function (evt) {
  initMap();
  init2dView();
  initLibcluon();
}

function initMap() {
  map =  L.map('global');
  let osm = new L.TileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png');		
  map.addLayer(osm);
  map.setView(new L.LatLng(57.70942631838934, 11.948822736740114), 17);

  markers = new L.FeatureGroup();
  map.addLayer(markers);
}

function init2dView() {
  canvas = document.getElementById("2d-view");
  canvas.width = 500;
  canvas.height = 500;
  const w = canvas.width;
  const h = canvas.height;

  if(canvas.getContext) {
    ctx = canvas.getContext("2d");
  }

  clear2dView();
}

function clear2dView() {
  const w = canvas.width;
  const h = canvas.height;

  ctx.clearRect(0, 0, w, h);

  ctx.beginPath();
  ctx.moveTo(w / 2, 0);
  ctx.lineTo(w / 2, h);
  ctx.stroke(); 
}

function drawObjects3dView() {
  clear2dView();

  Object.keys(currentLocalObjects).forEach(function(objectId) {
    const obj = currentLocalObjects[objectId];
    const type = obj["type"];
    const x = obj["position"]["x"];
    const y = obj["position"]["y"];

    const r = 5;

    let c = 'black';
    if (type == 0) {
      c = 'yellow';
    } else if (c == 1) {
      c = 'blue';
    } else if (c == 2) {
      c = 'red';
    }

    ctx.beginPath();
    ctx.arc(x, y, r, 0, 2 * Math.PI, false);
    ctx.fillStyle = c;
    ctx.fill();
  });
}

function initLibcluon() {
  var lc = libcluon();

  if ("WebSocket" in window) {
    var ws = new WebSocket("ws://" + window.location.host + "/", "od4");
    ws.binaryType = 'arraybuffer';

    ws.onopen = function() {
      onStreamOpen(ws, lc);
    }

    ws.onmessage = function(evt) {
      onMessageReceived(lc, evt.data);
    };

    ws.onclose = function() {
      onStreamClosed();
    };

    map.on('mouseup', function(e) {
      dataOut(lc, ws, 19, 0, "{\"latitude\":" + e.latlng.lat + ", \"longitude\":" + e.latlng.lng + "}");
    });

  } else {
    console.log("Error: websockets not supported by your browser.");
  }
}

function onStreamOpen(ws, lc) {
  function getResourceFrom(url) {
    var xmlHttp = new XMLHttpRequest();
    xmlHttp.open("GET", url, false);
    xmlHttp.send(null);
    return xmlHttp.responseText;
  }

  var odvd = getResourceFrom("opendlv-standard-message-set-v0.9.10.odvd");

  console.log("Connected to stream.");
  console.log("Loaded " + lc.setMessageSpecification(odvd) + " messages from specification.");
}

function onStreamClosed() {
  console.log("Disconnected from stream.");
}

function onMessageReceived(lc, msg) {

  var data_str = lc.decodeEnvelopeToJSON(msg);

  if (data_str.length == 2) {
    return;
  }

  d = JSON.parse(data_str);

  dataIn(d);
}

function dataIn(data) {
  if (d.dataType == 19) {
    const lat = d['opendlv_proxy_GeodeticWgs84Reading']['latitude'];
    const lon = d['opendlv_proxy_GeodeticWgs84Reading']['longitude'];
    if (!changedView) {
      map.setView(new L.LatLng(lat, lon), 18);
      changedView = true;
    }

    const c = L.circle([lat, lon], {
      color: "red",
      fillColor: "red",
      radius: 1
    });
    markers.addLayer(c);
  }
  if (d.dataType == 1128) {  // Frame start
    currentFrameId = d['opendlv_logic_perception_ObjectFrameStart']['objectFrameId'];
    currentLocalObjects = {};
    console.log("Frame start");
  }
  if (d.dataType == 1129) {  // Frame end
    drawObjects2dView();
    console.log("Frame end");
  }
  if (d.dataType == 1130) {  // Object
    const objectId = d['opendlv_logic_perception_Object']['objectId'];
    currentLocalObjects[objectId] = {};
  }
  if (d.dataType == 1131) {  // ObjectType
    const objectId = d['opendlv_logic_perception_ObjectType']['objectId'];
    const type = d['opendlv_logic_perception_ObjectType']['type'];
    currentLocalObjects[objectId] = {}; //Hack
    currentLocalObjects[objectId]["type"] = type;
    console.log("Object type: " + type + " for object id " + objectId);
  }
  if (d.dataType == 1136) {  // ObjectPosition
    const objectId = d['opendlv_logic_perception_ObjectPosition']['objectId'];
    const x = d['opendlv_logic_perception_ObjectPosition']['x'];
    const y = d['opendlv_logic_perception_ObjectPosition']['y'];
    const z = d['opendlv_logic_perception_ObjectPosition']['z'];
    currentLocalObjects[objectId]["position"] = {};
    currentLocalObjects[objectId]["position"]["x"] = x;
    currentLocalObjects[objectId]["position"]["y"] = y;
    currentLocalObjects[objectId]["position"]["z"] = z;
    console.log("Object position: " + x + "," + y + " for object id " + objectId);
  }

}

function dataOut(lc, ws, dataType, senderStamp, messageJson) {
  const message = lc.encodeEnvelopeFromJSONWithoutTimeStamps(messageJson, dataType, senderStamp);
  strToAb = str =>
    new Uint8Array(str.split('')
      .map(c => c.charCodeAt(0))).buffer;
  ws.send(strToAb(message), { binary: true });
}
