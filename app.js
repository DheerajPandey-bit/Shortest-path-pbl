// ==========================================
// SHORTEST PATH OPTIMIZER - FRONTEND
// Uses OpenStreetMap + C++ Dijkstra backend
// ==========================================

// HTML elements
const sourceInput = document.getElementById("source");
const destinationInput = document.getElementById("destination");
const findButton = document.getElementById("findRoute");
const message = document.getElementById("message");

// Initialize map
const map = L.map("map").setView([30.3165, 78.0322], 12);

L.tileLayer("https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png", {
    maxZoom: 19,
    attribution: '&copy; OpenStreetMap contributors'
}).addTo(map);

// Store map objects
let routeLayers = [];
let markers = [];

// Clear previous routes and markers
function clearMap() {
    routeLayers.forEach(layer => map.removeLayer(layer));
    markers.forEach(marker => map.removeLayer(marker));

    routeLayers = [];
    markers = [];
}

// ==========================================
// STEP 1: CONVERT PLACE NAME INTO COORDINATES
// ==========================================

async function geocodePlace(place) {

    const url = new URL(
        "https://nominatim.openstreetmap.org/search"
    );

    url.search = new URLSearchParams({
        q: place,
        format: "jsonv2",
        limit: "1"
    });

    const response = await fetch(url.toString(), {
        headers: {
            Accept: "application/json"
        }
    });

    if (!response.ok) {
        throw new Error(
            "Location lookup failed. Please try again."
        );
    }

    const results = await response.json();

    if (!results.length) {
        throw new Error(
            `Location not found: ${place}. Include city and state.`
        );
    }

    return {
        name: results[0].display_name,
        lat: Number(results[0].lat),
        lng: Number(results[0].lon)
    };
}

// ==========================================
// STEP 2: DRAW ROUTE ON MAP
// ==========================================

function drawRoute(route, color, dashed = false) {

    if (!route || !route.nodes || route.nodes.length === 0) {
        return null;
    }

    const coordinates = route.nodes.map(node => [
        node.lat,
        node.lng
    ]);

    const line = L.polyline(coordinates, {
        color: color,
        weight: 6,
        opacity: 0.9,
        dashArray: dashed ? "9 9" : null
    }).addTo(map);

    routeLayers.push(line);

    return line;
}

// ==========================================
// STEP 3: DISPLAY ROUTE DETAILS
// ==========================================

function showRouteCard(
    route,
    distanceId,
    timeId,
    pathId
) {

    const distanceElement = document.getElementById(distanceId);
    const timeElement = document.getElementById(timeId);
    const pathElement = document.getElementById(pathId);

    if (!route) {

        distanceElement.textContent = "Unavailable";

        timeElement.textContent =
            "No alternative road route found.";

        pathElement.textContent = "";

        return;
    }

    distanceElement.textContent =
        `${Number(route.distanceKm).toFixed(2)} km`;

    timeElement.textContent =
        `Estimated time: ${Number(route.minutes).toFixed(1)} minutes`;

    pathElement.textContent =
        `${route.nodes.length} road points`;
}

// ==========================================
// STEP 4: MAIN ROUTE CALCULATION
// ==========================================

async function findRoute() {

    const sourceText = sourceInput.value.trim();
    const destinationText = destinationInput.value.trim();

    // Validate inputs
    if (!sourceText || !destinationText) {

        message.textContent =
            "Please enter both starting location and destination.";

        return;
    }

    findButton.disabled = true;

    clearMap();

    message.textContent =
        "Finding locations and downloading road network...";

    try {

        // Convert manually entered places into coordinates
        const [source, destination] = await Promise.all([

            geocodePlace(sourceText),

            geocodePlace(destinationText)

        ]);

        // Send coordinates to C++ backend
        const params = new URLSearchParams({

            sourceLat: source.lat,
            sourceLng: source.lng,

            destinationLat: destination.lat,
            destinationLng: destination.lng,

            mode: document.getElementById("mode").value,

            traffic: document.getElementById("traffic").value

        });

        // Call C++ Dijkstra API
        const response = await fetch(
            `/api/route?${params.toString()}`
        );

        const data = await response.json();

        if (!response.ok) {

            throw new Error(
                data.error || "Route calculation failed."
            );

        }

        // Display best route
        showRouteCard(
            data.best,
            "bestDistance",
            "bestTime",
            "bestPath"
        );

        // Display alternative route
        showRouteCard(
            data.alternative,
            "altDistance",
            "altTime",
            "altPath"
        );

        // Draw recommended route in blue
        const bestLine = drawRoute(
            data.best,
            "#2563eb"
        );

        // Draw alternative route in orange
        drawRoute(
            data.alternative,
            "#f59e0b",
            true
        );

        // Add starting location marker
        markers.push(

            L.marker([
                source.lat,
                source.lng
            ])
            .bindPopup(
                `<b>Start:</b> ${escapeHtml(sourceText)}`
            )
            .addTo(map)

        );

        // Add destination marker
        markers.push(

            L.marker([
                destination.lat,
                destination.lng
            ])
            .bindPopup(
                `<b>Destination:</b> ${escapeHtml(destinationText)}`
            )
            .addTo(map)

        );

        // Zoom map to recommended route
        if (bestLine) {

            map.fitBounds(
                bestLine.getBounds(),
                {
                    padding: [35, 35]
                }
            );

        }

        message.textContent =
            "Route calculated using C++ Dijkstra on OpenStreetMap road data. Travel times are estimates, not live traffic.";

    }

    catch (error) {

        console.error(error);

        message.textContent = error.message;

    }

    finally {

        findButton.disabled = false;

    }

}

// ==========================================
// STEP 5: PREVENT HTML INJECTION IN POPUPS
// ==========================================

function escapeHtml(value) {

    return value.replace(/[&<>"']/g, char => ({

        "&": "&amp;",
        "<": "&lt;",
        ">": "&gt;",
        '"': "&quot;",
        "'": "&#39;"

    }[char]));

}

// ==========================================
// STEP 6: BUTTON EVENT
// ==========================================

findButton.addEventListener(
    "click",
    findRoute
);

// Allow Enter key to calculate route
[sourceInput, destinationInput].forEach(input => {

    input.addEventListener("keydown", event => {

        if (event.key === "Enter") {
            findRoute();
        }

    });

});