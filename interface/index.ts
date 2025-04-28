const { SerialPort } = require('serialport');
const readline = require('readline');

// Replace with your ESP32's serial port
const portPath = '/dev/tty.usbserial-59170078541';

// Create a new serial port instance
const port = new SerialPort({
    path: portPath,
    baudRate: 115200, // Match the baud rate in your ESP32 code
});

// Create a readline interface for user input
const rl = readline.createInterface({
    input: process.stdin,
    output: process.stdout,
});




// Open the port and handle commands
port.on('open', () => {
    console.log('Serial port opened.');

    // Prompt the user for input
    rl.setPrompt('Enter command to send to ESP32: ');
    rl.prompt();

    // Handle user input
    rl.on('line', (input: string) => {
        // Send the user input as a command to the ESP32
        port.write(input, (err: { message: any }) => {
            if (err) {
                console.error('Error writing to serial port:', err.message);
            } else {
                console.log(`Command "${input}" sent to ESP32.`);
            }
        });

        // Prompt for the next command
        rl.prompt();
    });
});

// Handle data received from the ESP32
port.on('data', (data: { toString: () => any }) => {
    console.log('Received from ESP32:', data.toString());
});

// Handle errors
port.on('error', (err: { message: any }) => {
    console.error('Serial port error:', err.message);
});

// Handle program exit
rl.on('close', () => {
    console.log('Exiting program.');
    process.exit(0);
});