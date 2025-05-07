const http = require('http');
const WebSocketServer = require('ws');

const { SerialPort } = require('serialport');
const readline = require('readline');

interface WebSocketWithSerialPort extends WebSocket {
    send(data: string, cb?: (err?: Error) => void): void;
    on(event: string, listener: (data: any) => void): this;
}


const rl = readline.createInterface({
    input: process.stdin,
    output: process.stdout,
});

// Replace with your ESP32's serial port
const portPath = '/dev/tty.usbserial-59170079241';

// Create a new serial port instance
const port = new SerialPort({
    path: portPath,
    baudRate: 115200, // Match the baud rate in your ESP32 code
});

// Create a new HTTP server
const server = http.createServer();
const wss = new WebSocketServer.Server({ server });


// // Prompt the user for input
// rl.prompt();

// // Handle user input
// rl.on('line', (input: string) => {
//     console.log(`Sending to ESP32: ${input}`);
//     // Send the input to the serial port
//     port.write(input, (err?: Error) => {
//         if (err) {
//             console.error('Error writing to serial port:', err.message);
//         } else {
//             console.log(`Sent to serial port: ${input}`);
//         }
//     });
//     rl.prompt();
// }
// );

wss.on('connection', (ws: WebSocketWithSerialPort) => {
    console.log('New client connected');

    // Handle messages from WebSocket clients
    ws.on('message', (message: string) => {
        console.log(`Received from WebSocket: ${message}`);
        // Send the message to the serial port
        port.write(message, (err?: Error) => {
            if (err) {
                console.error('Error writing to serial port:', err.message);
                ws.send(`Error: ${err.message}`);
            } else {
                console.log(`Sent to serial port: ${message}`);
            }
        });
    });

    // Handle serial port data and send it to WebSocket clients
    port.on('data', (data: Buffer) => {
        console.log(`Received from serial port: ${data.toString()}`);
        ws.send(data.toString());
    });

    ws.on('close', () => {
        console.log('Client disconnected');
    });
});

// also send 

server.listen(8080, () => {
    console.log('WebSocket server is running on ws://localhost:8080');
});

