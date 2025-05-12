# Hydroponic Garden System

This is the hardware and interface component for an ESP32 hydroponic garden. The ESP32 communicates over UART and serial to a node.js program. That receives the data and streams the data over sockets to the web GUI.

## Running the web interface

Assuming your ESP32 is flashed with the right software for broadcasting over the UART port, here's how you can use the web interface.

Make sure you have `node.js` installed. Instructions [here](https://nodejs.org/en/download).

Once you have the necessary prerequisites and cloning this repo.

```
cd interface
```

```
npm install
```

```
npm run start
```

In another terminal:

```
cd web
```

```
npm install
```

```
npm run dev
```

This should get you started with running the web interface locally. Any changes you do in `web/src/components/home.astro` should be reflected immediately.
