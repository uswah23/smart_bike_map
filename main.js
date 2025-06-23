document.addEventListener("DOMContentLoaded", function () {
  const map = new ol.Map({
    target: 'map',
    layers: [new ol.layer.Tile({ source: new ol.source.OSM() })],
    view: new ol.View({
      center: ol.proj.fromLonLat([103.0902, 1.8576]),
      zoom: 15.5
    })
  });

  const vectorSource = new ol.source.Vector();
  const markerLayer = new ol.layer.Vector({ source: vectorSource });
  map.addLayer(markerLayer);

  // UTHM marker and geofence
  const geojsonBoundary = {
    type: "FeatureCollection",
    features: [{
      type: "Feature",
      geometry: {
        type: "Polygon",
        coordinates: [[
          [103.09135876179585, 1.859438962982793],
          [103.09019716545163, 1.8667660511367217],
          [103.08046556940644, 1.8636701021144688],
          [103.080981834449, 1.858845570707473],
          [103.07935559956519, 1.8582521782326893],
          [103.07961373208644, 1.8575555868131346],
          [103.0784779489938, 1.8572717902294613],
          [103.07883933452257, 1.8556980083496626],
          [103.07963954533801, 1.855930205764352],
          [103.08118834046576, 1.8507702560332433],
          [103.09172014732474, 1.8542274240157468],
          [103.09019716545163, 1.8591293670382925],
          [103.09135876179585, 1.859438962982793]
        ]]
      }
    }]
  };
  const geojsonFormat = new ol.format.GeoJSON();
  const boundaryFeatures = geojsonFormat.readFeatures(geojsonBoundary, {
    featureProjection: 'EPSG:3857'
  });
  boundaryFeatures.forEach(f => f.setStyle(new ol.style.Style({
    stroke: new ol.style.Stroke({ color: 'rgba(255, 0, 238, 1)', width: 2 }),
    fill: new ol.style.Fill({ color: 'rgba(252, 70, 203, 0.2)' })
  })));
  vectorSource.addFeatures(boundaryFeatures);

  const popup = new ol.Overlay({
    element: document.getElementById('popup'),
    positioning: 'bottom-center',
    stopEvent: false,
    offset: [0, -30]
  });
  map.addOverlay(popup);
  map.on('singleclick', function (evt) {
    const feature = map.forEachFeatureAtPixel(evt.pixel, feat => feat);
    if (feature) {
      popup.setPosition(feature.getGeometry().getCoordinates());
      document.getElementById('popup-content').innerHTML = `<strong>${feature.get('name')}</strong>`;
    } else {
      popup.setPosition(undefined);
    }
  });

  let bikeMarker = null;
  let trailLine = new ol.geom.LineString([]);
  let trailFeature = new ol.Feature({ geometry: trailLine });
  trailFeature.setStyle(new ol.style.Style({
    stroke: new ol.style.Stroke({ color: '#0073e6', width: 2 })
  }));
  vectorSource.addFeature(trailFeature);

  let userCoords = null;
  navigator.geolocation.getCurrentPosition(pos => {
    userCoords = [pos.coords.longitude, pos.coords.latitude];
    const userMarker = new ol.Feature({
      geometry: new ol.geom.Point(ol.proj.fromLonLat(userCoords)),
      name: 'You'
    });
    userMarker.setStyle(new ol.style.Style({
      image: new ol.style.Icon({
        src: 'https://cdn-icons-png.flaticon.com/512/1077/1077012.png',
        scale: 0.05
      })
    }));
    vectorSource.addFeature(userMarker);
  });

  const stopButton = document.getElementById("stop-btn");
  let overrideBuzzer = false;

  const ws = new WebSocket('ws://192.168.112.98/gps'); // Update with your ESP32 IP

  stopButton.addEventListener("click", function () {
    overrideBuzzer = true;
    stopButton.style.display = "none";
    if (ws.readyState === WebSocket.OPEN) {
      ws.send("STOP_BUZZER");
    }
    fetch("https://api.telegram.org/bot<YOUR_BOT_TOKEN>/sendMessage", {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        chat_id: "<YOUR_CHAT_ID>",
        text: "🛑 Buzzer manually stopped by user."
      })
    });
  });

  ws.onmessage = function (event) {
    const data = JSON.parse(event.data);
    const coords = ol.proj.fromLonLat([data.lon, data.lat]);

    document.getElementById("lat").textContent = data.lat.toFixed(6);
    document.getElementById("lon").textContent = data.lon.toFixed(6);

    const inUTHM = boundaryFeatures.some(f => f.getGeometry().intersectsCoordinate(coords));

    if (!inUTHM && !overrideBuzzer && (!bikeMarker || !bikeMarker.get('alerted'))) {
      alert("🚨 You have exited the UTHM geofence!");
      if (bikeMarker) bikeMarker.set('alerted', true);
      stopButton.style.display = 'block';
    } else if (inUTHM && bikeMarker) {
      bikeMarker.set('alerted', false);
      overrideBuzzer = false;
      stopButton.style.display = 'none';
    }

    if (!bikeMarker) {
      bikeMarker = new ol.Feature({
        geometry: new ol.geom.Point(coords),
        name: "Bicycle"
      });
      bikeMarker.setStyle(new ol.style.Style({
        image: new ol.style.Icon({
          anchor: [0.5, 1],
          src: 'https://cdn-icons-png.flaticon.com/512/2972/2972185.png',
          scale: 0.07,
          rotateWithView: true
        })
      }));
      vectorSource.addFeature(bikeMarker);
    } else {
      bikeMarker.getGeometry().setCoordinates(coords);
    }

    trailLine.appendCoordinate(coords);
    trailFeature.getGeometry().setCoordinates(trailLine.getCoordinates());
  };
});
