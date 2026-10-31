# Shortest Path Optimizer

A C++17 route-optimization prototype with a browser-based Leaflet map.

## Features
- Manually enter a starting place and destination.
- OpenStreetMap Nominatim resolves the typed place names to coordinates.
- Each entered place is mapped to its nearest node in the sample C++ road graph.
- C++ uses Dijkstra's algorithm to calculate a route by distance or estimated travel time.
- A distinct alternative is searched by temporarily banning each edge of the best route and rerunning Dijkstra.
- Leaflet displays the searched locations and the sample-graph routes.

## Requirements
- Windows
- MSYS2 UCRT64 GCC compiler
- Internet connection for Leaflet, OpenStreetMap tiles, and geocoding

## Setup
1. Open PowerShell in the project folder.
2. Download the C++ HTTP header:

   ```powershell
   Invoke-WebRequest https://raw.githubusercontent.com/yhirose/cpp-httplib/master/httplib.h -OutFile backend/httplib.h
   ```

3. Compile using the UCRT64 compiler explicitly:

   ```powershell
   & "C:\msys64\ucrt64\bin\g++.exe" -std=c++17 -O2 -pthread backend/main.cpp -o backend/server.exe -lws2_32
   ```

4. Start the server:

   ```powershell
   .\backend\server.exe
   ```

5. Open http://localhost:8090 in your browser.

Press Ctrl+C in PowerShell to stop the server.
