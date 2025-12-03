<?php
// ysf_command.php
header('Content-Type: application/json; charset=utf-8');

function response($ok, $payload = []) {
    http_response_code($ok ? 200 : (http_response_code() ?: 400));
    echo json_encode(
        array_merge(['ok' => $ok], $payload),
        JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES
    );
    exit;
}

// später aus Datei/DB, jetzt hart codiert – wie in save_config.php
$CONFIG_PASSWORD = 'setuppassword';

// Nur POST akzeptieren
if (($_SERVER['REQUEST_METHOD'] ?? '') !== 'POST') {
    http_response_code(405);
    response(false, ['error' => 'method not allowed']);
}

/* ======= Passwort prüfen ======= */
$givenPw = $_POST['ConfigPassword'] ?? '';
if ($CONFIG_PASSWORD === '' || !hash_equals($CONFIG_PASSWORD, (string)$givenPw)) {
    http_response_code(401);
    response(false, ['error' => 'auth required']);
}
// nie weiterverarbeiten/mitzählen
unset($_POST['ConfigPassword']);

/* ======= Rest: UDP-Kommando ======= */

$host = '127.0.0.1';
$port = 6073;

$cmd = $_POST['cmd'] ?? '';
$cmd = trim($cmd);

if ($cmd !== 'disconnect' && $cmd !== 'connect') {
    response(false, ['error' => 'Invalid command']);
}

if ($cmd === 'disconnect') {
    $payload = 'UnLink';
} else {
    $reflector = $_POST['reflector'] ?? '';
    $reflector = trim($reflector);

    if ($reflector === '') {
        response(false, ['error' => 'Missing reflector']);
    }

    $payload = 'LinkYSF ' . $reflector;
}

$errno  = 0;
$errstr = '';

$socket = @stream_socket_client(
    "udp://{$host}:{$port}",
    $errno,
    $errstr,
    1.0,
    STREAM_CLIENT_CONNECT
);

if (!$socket) {
    response(false, ['error' => "Socket error: {$errstr} ({$errno})"]);
}

$written = @fwrite($socket, $payload);
@fclose($socket);

if ($written === false || $written === 0) {
    response(false, ['error' => 'Failed to send UDP payload']);
}

response(true, ['sent' => $payload]);
