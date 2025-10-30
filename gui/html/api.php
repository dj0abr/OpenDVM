<?php
declare(strict_types=1);

/**
 * Kleine JSON-API für MMDVM-Daten.
 * - Gibt je nach $_GET['q'] unterschiedliche Auswertungen zurück.
 * - Antwort ist immer JSON (UTF-8).
 */
header('Content-Type: application/json; charset=utf-8');

/**
 * Ermittelt den Ländercode (ISO-3166 Alpha-2) anhand eines Rufzeichen-Präfixes.
 * Hinweis:
 * - Mapping ist bewusst statisch und unvollständig/uneinheitlich (DX real life).
 * - Reihenfolge ist wichtig: längere Präfixe müssen davor stehen (z. B. "OH0" vor "OH").
 * - Keine Logikänderung: Mapping bleibt exakt wie vorgefunden, inkl. Duplikate.
 *
 * @param mixed $call Rufzeichen (beliebiger Typ, wird intern zu String gecastet)
 * @return string|null ISO-3166-Code oder null, wenn kein Match
 */
function prefix_to_country($call) {
  static $map = null;

  if ($map === null) {
    $map = [
      // --- Europa ---
      'DL' => 'DE', 'DA' => 'DE', 'DB' => 'DE', 'DC' => 'DE', 'DD' => 'DE', 'DE' => 'DE', 'DF' => 'DE', 'DG' => 'DE', 'DH' => 'DE', 'DJ' => 'DE', 'DK' => 'DE', 'DM' => 'DE', 'DN' => 'DE', 'DO' => 'DE',
      'OE' => 'AT', 'OK' => 'CZ', 'OM' => 'SK', 'HA' => 'HU', 'SP' => 'PL', 'S5' => 'SI', '9A' => 'HR', 'YU' => 'RS', 'YT' => 'RS', 'YL' => 'LV', 'ES' => 'EE', 'LY' => 'LT',
      'OH' => 'FI', 'SM' => 'SE', 'LA' => 'NO', 'OZ' => 'DK', 'TF' => 'IS', 'EI' => 'IE', 'PA' => 'NL', 'ON' => 'BE', 'LX' => 'LU', 'HB9' => 'CH', 'HB3' => 'CH', 'HB0' => 'LI',
      'F' => 'FR', 'TM' => 'FR', 'TK' => 'FR', 'EA' => 'ES', 'EB' => 'ES', 'EC' => 'ES', 'ED' => 'ES', 'EE' => 'ES', 'EF' => 'ES', 'EG' => 'ES', 'EH' => 'ES', 'CT' => 'PT', 'CU' => 'PT',
      'I' => 'IT', 'IS' => 'IT', 'IZ' => 'IT', 'IN' => 'IT', 'IW' => 'IT', 'IV' => 'IT', 'SV' => 'GR', 'SW' => 'GR', 'SX' => 'GR', 'SY' => 'GR',
      'YO' => 'RO', 'YR' => 'RO', 'LZ' => 'BG', 'E7' => 'BA', 'Z3' => 'MK', '9H' => 'MT', 'ER' => 'MD', 'UA2' => 'RU', 'R2' => 'RU', 'R3' => 'RU', 'UA3' => 'RU', 'UA1' => 'RU', 'R1' => 'RU',
      'UA' => 'RU', 'UB' => 'RU', 'UC' => 'RU', 'UD' => 'RU', 'UE' => 'RU', 'UF' => 'RU', 'UG' => 'RU', 'UH' => 'RU', 'UI' => 'RU',
      'US' => 'UA', 'UR' => 'UA', 'UT' => 'UA', 'UU' => 'UA', 'UV' => 'UA', 'UW' => 'UA', 'UX' => 'UA', 'UY' => 'UA', 'UZ' => 'UA',
      'LY' => 'LT', 'ES' => 'EE', 'OH0' => 'AX', 'OY' => 'FO', 'OX' => 'GL', 'TF' => 'IS',
      'CN' => 'MA', 'EA8' => 'ES', 'CT9' => 'PT', 'IS0' => 'IT',
      'TA' => 'TR', 'TC' => 'TR',

      // --- Vereinigtes Königreich ---
      'G' => 'GB', 'M' => 'GB', '2E' => 'GB', 'GM' => 'GB', 'GW' => 'GB', 'GI' => 'GB', 'GD' => 'GB', 'GU' => 'GB', 'GH' => 'GB', 'GT' => 'GB', 'MB' => 'GB', 'GB' => 'GB',

      // --- Skandinavien & Ostsee ---
      'LA' => 'NO', 'LB' => 'NO', 'LC' => 'NO', 'LD' => 'NO', 'LG' => 'NO', 'LH' => 'NO', 'LI' => 'NO', 'LN' => 'NO',
      'OH' => 'FI', 'OF' => 'FI', 'OG' => 'FI', 'OJ' => 'FI', 'OH0' => 'AX',
      'SM' => 'SE', '7S' => 'SE', 'SB' => 'SE', 'SI' => 'SE', 'SL' => 'SE',
      'OZ' => 'DK', 'OV' => 'DK', '5P' => 'DK', '5Q' => 'DK',

      // --- Nordamerika ---
      'K' => 'US', 'N' => 'US', 'W' => 'US', 'AA' => 'US', 'AB' => 'US', 'AC' => 'US', 'AD' => 'US', 'AE' => 'US', 'AF' => 'US', 'AG' => 'US', 'AI' => 'US', 'AJ' => 'US', 'AK' => 'US',
      'KL' => 'US', 'KH6' => 'US', 'WH6' => 'US', 'KH7' => 'US', 'KH8' => 'AS', 'KH9' => 'UM', 'KP4' => 'PR', 'KP2' => 'VI', 'NP4' => 'PR', 'WP4' => 'PR',
      'VE' => 'CA', 'VA' => 'CA', 'VY' => 'CA', 'VO' => 'CA', 'CY' => 'CA', 'CZ' => 'CA', 'CG' => 'CA',

      // --- Mittel- & Südamerika ---
      'HC' => 'EC', 'HD' => 'EC', 'OA' => 'PE', 'OB' => 'PE', 'TI' => 'CR', 'TE' => 'CR', 'TG' => 'GT', 'YN' => 'NI', 'YS' => 'SV', 'HP' => 'PA', 'HO' => 'PA', 'HH' => 'HT',
      'HI' => 'DO', 'CP' => 'BO', 'CE' => 'CL', 'CA' => 'CL', 'CB' => 'CL', 'CC' => 'CL', 'CD' => 'CL', '3G' => 'CL',
      'CX' => 'UY', 'LU' => 'AR', 'LW' => 'AR', 'LR' => 'AR', 'LS' => 'AR', 'LT' => 'AR', 'LV' => 'AR', 'PU' => 'BR', 'PY' => 'BR', 'PP' => 'BR', 'PQ' => 'BR', 'PR' => 'BR',
      'PZ' => 'SR', 'HC8' => 'EC', 'PJ2' => 'CW', 'PJ4' => 'BQ', 'PJ5' => 'BQ', 'PJ7' => 'BQ',

      // --- Karibik ---
      'J3' => 'GD', 'J7' => 'DM', 'J8' => 'VC', '9Y' => 'TT', '9Z' => 'TT', 'VP2E' => 'AI', 'VP2M' => 'MS', 'VP2V' => 'VG', 'VP5' => 'TC', 'VP6' => 'PN', 'VP9' => 'BM', 'ZF' => 'KY',
      'CM' => 'CU', 'CO' => 'CU', 'T4' => 'CU', 'C6' => 'BS', 'PJ' => 'BQ',

      // --- Afrika ---
      'ZS' => 'ZA', 'ZR' => 'ZA', 'ZU' => 'ZA', '5R' => 'MG', '5T' => 'MR', '5U' => 'NE', '5V' => 'TG', '5X' => 'UG', '5Z' => 'KE', '6O' => 'SO', '6V' => 'SN', '6W' => 'SN', '7O' => 'YE',
      '7P' => 'LS', '7Q' => 'MW', '7X' => 'DZ', '9G' => 'GH', '9J' => 'ZM', '9L' => 'SL', '9Q' => 'CD', '9U' => 'BI', '9X' => 'RW', 'D2' => 'AO', 'D4' => 'CV', 'D6' => 'KM',
      'EL' => 'LR', 'ET' => 'ET', 'S7' => 'SC', 'ST' => 'SD', 'SU' => 'EG', 'TJ' => 'CM', 'TN' => 'CG', 'TR' => 'GA', 'TT' => 'TD', 'TZ' => 'ML', 'V5' => 'NA', 'ZD7' => 'SH', 'ZD8' => 'SH', 'ZD9' => 'SH',

      // --- Naher Osten ---
      '4X' => 'IL', '4Z' => 'IL', '5B' => 'CY', 'C4' => 'CY', 'H2' => 'CY', 'E3' => 'ER', 'EK' => 'AM', 'EP' => 'IR', 'EQ' => 'IR', 'HZ' => 'SA', '7Z' => 'SA', '8Z' => 'SA', 'A4' => 'OM',
      'A6' => 'AE', 'A7' => 'QA', 'A9' => 'BH', 'AP' => 'PK', 'YA' => 'AF', 'T6' => 'AF', 'YK' => 'SY', 'YI' => 'IQ', '9K' => 'KW',

      // --- Asien ---
      'VU' => 'IN', 'VT' => 'IN', 'AT' => 'IN', '8T' => 'IN', '8Q' => 'MV', '9N' => 'NP', 'EY' => 'TJ', 'EX' => 'KG', 'EZ' => 'TM', 'HL' => 'KR', 'DS' => 'KR', 'DT' => 'KR',
      'JA' => 'JP', 'JE' => 'JP', 'JF' => 'JP', 'JG' => 'JP', 'JH' => 'JP', 'JI' => 'JP', 'JJ' => 'JP', 'JK' => 'JP', 'JL' => 'JP', 'JM' => 'JP', 'JN' => 'JP', 'JO' => 'JP', 'JR' => 'JP',
      'BV' => 'TW', 'BX' => 'TW', 'BY' => 'CN', 'BD' => 'CN', 'BG' => 'CN', 'BH' => 'CN', 'BL' => 'CN', 'BM' => 'CN', 'BN' => 'CN', 'BT' => 'CN',
      'HS' => 'TH', 'E2' => 'TH', '9M2' => 'MY', '9M6' => 'MY', '9M8' => 'MY', '9V' => 'SG', 'YB' => 'ID', 'YE' => 'ID', 'PK' => 'ID', 'PL' => 'ID', 'PM' => 'ID', 'PN' => 'ID',
      '9M' => 'MY', '9V' => 'SG', '9W' => 'MY', 'VR' => 'HK', 'DU' => 'PH', 'DV' => 'PH', 'DW' => 'PH', 'DX' => 'PH', 'DY' => 'PH', 'DZ' => 'PH', 'VU' => 'IN',

      // --- Ozeanien ---
      'VK' => 'AU', 'AX' => 'AU', 'VI' => 'AU', 'ZL' => 'NZ', '3D2' => 'FJ', 'A3' => 'TO', 'E5' => 'CK', 'T30' => 'KI', 'T31' => 'KI', 'T32' => 'KI', 'T33' => 'KI',
      '5W' => 'WS', 'YJ' => 'VU', 'P2' => 'PG', 'C2' => 'NR', 'T2' => 'TV', 'ZK1' => 'CK', 'ZK3' => 'TK', 'A2' => 'BW', 'H40' => 'SB', 'H44' => 'SB', 'FK' => 'NC', 'FO' => 'PF', 'FW' => 'WF',

      // --- Antarktis & Gebiete ---
      'VP8' => 'FK', 'CE9' => 'AQ', 'RI1A' => 'AQ', 'DP1' => 'AQ', 'KC4' => 'AQ', 'LU1Z' => 'AQ', 'VK0' => 'AQ', 'ZL5' => 'AQ', 'ZS7' => 'AQ',

      // --- Sonderrufe ---
      'AM' => 'ES', 'AN' => 'ES', 'AO' => 'ES', 'EG' => 'ES', 'EH' => 'ES', 'EM' => 'UA', 'EN' => 'UA', 'EO' => 'UA'
    ];
  }

  // Eingabe aufbereiten
  $call = strtoupper(trim($call ?? ''));
  if ($call === '') {
    return null;
  }

  // Präfix-Match (greedy von links nach rechts)
  foreach ($map as $pfx => $cc) {
    if (str_starts_with($call, $pfx)) {
      return $cc;
    }
  }

  return null;
}

try {
  /**
   * DB-Verbindung (Unix-Socket, kein TCP).
   * - ERRMODE_EXCEPTION: Fehler werden als Exceptions geworfen.
   * - FETCH_ASSOC: Ergebnisse als assoziative Arrays.
   * - EMULATE_PREPARES=false: native Prepared Statements, wenn verfügbar.
   */
  $pdo = new PDO(
    'mysql:unix_socket=/run/mysqld/mysqld.sock;dbname=mmdvmdb;charset=utf8mb4',
    'www-data',
    '',
    [
      PDO::ATTR_ERRMODE            => PDO::ERRMODE_EXCEPTION,
      PDO::ATTR_DEFAULT_FETCH_MODE => PDO::FETCH_ASSOC,
      PDO::ATTR_EMULATE_PREPARES   => false,
    ]
  );

  // Abfrageparameter: q=...
  $q = $_GET['q'] ?? 'status';

  /* =========================
     Status (Einzelzeile)
     ========================= */
  if ($q === 'status') {
    $row = $pdo->query(
      "SELECT id, mode, callsign, dgid, source, active, ber, duration,
              DATE_FORMAT(updated_at, '%Y-%m-%d %H:%i:%s') AS updated_at
       FROM status
       WHERE id = 1"
    )->fetch();

    if ($row) {
      $row['country_code'] = prefix_to_country($row['callsign'] ?? null);
    }

    echo json_encode($row ?: [], JSON_UNESCAPED_UNICODE);
    exit;
  }

  /* =========================
     Lastheard (Top 10, zuletzt zuerst)
     ========================= */
  if ($q === 'lastheard') {
    $rows = $pdo->query("
      SELECT callsign, mode, dgid, source, duration, ber,
             DATE_FORMAT(ts, '%Y-%m-%d %H:%i:%s') AS ts
      FROM lastheard
      ORDER BY ts DESC
      LIMIT 10
    ")->fetchAll();

    foreach ($rows as &$r) {
      $r['country_code'] = prefix_to_country($r['callsign']);
    }

    echo json_encode($rows, JSON_UNESCAPED_UNICODE);
    exit;
  }

  /* =========================
     Aktivität 48h: RF vs. NET pro Stunde
     ========================= */
  if ($q === 'activity48h') {
    $rows = $pdo->query(
      "SELECT DATE_FORMAT(ts, '%Y-%m-%d %H:00:00') AS hour,
              SUM(source = 'RF')  AS rf,
              SUM(source = 'NET') AS net
       FROM lastheard
       WHERE ts >= NOW() - INTERVAL 48 HOUR
       GROUP BY hour
       ORDER BY hour ASC"
    )->fetchAll();

    echo json_encode($rows, JSON_UNESCAPED_UNICODE);
    exit;
  }

  /* =========================
     Aktivität 48h: nach Mode (aggregiert)
     ========================= */
  if ($q === 'activityByMode48h') {
    $rows = $pdo->query("
      SELECT mode_norm, COUNT(*) AS cnt
      FROM (
        SELECT
          CASE
            WHEN mode LIKE 'D-Star%'                             THEN 'dstar'
            WHEN mode LIKE 'System Fusion%' OR mode LIKE 'YSF%' THEN 'ysf'
            WHEN mode LIKE 'DMR%'                                THEN 'dmr'
            ELSE NULL
          END AS mode_norm
        FROM lastheard
        WHERE ts >= NOW() - INTERVAL 48 HOUR
      ) t
      WHERE mode_norm IS NOT NULL
      GROUP BY mode_norm
    ")->fetchAll();

    $out = ['dstar' => 0, 'ysf' => 0, 'dmr' => 0];
    foreach ($rows as $r) {
      $out[strtolower($r['mode_norm'])] = (int) $r['cnt'];
    }

    echo json_encode($out, JSON_UNESCAPED_UNICODE);
    exit;
  }

  /* =========================
     Aktivität 48h: nach Mode & Source (RF/NET) gesplittet
     ========================= */
  if ($q === 'activityByMode48hSplit') {
    $rows = $pdo->query("
      SELECT mode_norm, UPPER(source) AS src, COUNT(*) AS cnt
      FROM (
        SELECT
          CASE
            WHEN mode LIKE 'D-Star%'                             THEN 'dstar'
            WHEN mode LIKE 'System Fusion%' OR mode LIKE 'YSF%' THEN 'ysf'
            WHEN mode LIKE 'DMR%'                                THEN 'dmr'
            ELSE NULL
          END AS mode_norm,
          source, ts
        FROM lastheard
        WHERE ts >= NOW() - INTERVAL 48 HOUR
      ) t
      WHERE mode_norm IS NOT NULL
        AND (source = 'RF' OR source = 'NET')
      GROUP BY mode_norm, src
    ")->fetchAll();

    $out = [
      'dstar' => ['RF' => 0, 'NET' => 0],
      'ysf'   => ['RF' => 0, 'NET' => 0],
      'dmr'   => ['RF' => 0, 'NET' => 0],
    ];

    foreach ($rows as $r) {
      $m = strtolower($r['mode_norm']);
      $s = ($r['src'] === 'RF' ? 'RF' : 'NET');
      $out[$m][$s] = (int) $r['cnt'];
    }

    echo json_encode($out, JSON_UNESCAPED_UNICODE);
    exit;
  }

  /* =========================
     Heatmap 30d: Count pro Wochentag/Stunde
     ========================= */
  if ($q === 'heatmap30d') {
    $rows = $pdo->query("
      SELECT
        DAYOFWEEK(ts) AS dow,  -- 1=Sonntag, 7=Samstag
        HOUR(ts)      AS hh,
        COUNT(*)      AS cnt
      FROM lastheard
      WHERE ts >= NOW() - INTERVAL 30 DAY
      GROUP BY dow, hh
      ORDER BY dow, hh
    ")->fetchAll();

    echo json_encode($rows, JSON_UNESCAPED_UNICODE);
    exit;
  }

  /* =========================
     ⬇︎ NEU: Auswertungskacheln
     ========================= */

  // Durchschnittliche Sendedauer pro Mode
  if ($q === 'avgDurationByMode') {
    $rows = $pdo->query("
      SELECT mode AS mode, ROUND(AVG(duration), 3) AS avg
      FROM lastheard
      WHERE mode IS NOT NULL AND duration IS NOT NULL
      GROUP BY mode
      ORDER BY mode
    ")->fetchAll();

    echo json_encode($rows, JSON_UNESCAPED_UNICODE);
    exit;
  }

  // Anzahl QSOs pro Callsign (Top 10)
  if ($q === 'callsignTop10Count') {
    $rows = $pdo->query("
      SELECT UPPER(callsign) AS callsign, COUNT(*) AS cnt
      FROM lastheard
      WHERE callsign IS NOT NULL AND callsign <> ''
      GROUP BY UPPER(callsign)
      ORDER BY cnt DESC
      LIMIT 10
    ")->fetchAll();

    // Optional: Country-Code ergänzen (reine Darstellung)
    foreach ($rows as &$r) {
      $r['country_code'] = prefix_to_country($r['callsign'] ?? null);
    }

    echo json_encode($rows, JSON_UNESCAPED_UNICODE);
    exit;
  }

  // Gesamtsendezeit pro Callsign (Top 10)
  if ($q === 'callsignTop10Duration') {
    $rows = $pdo->query("
      SELECT UPPER(callsign) AS callsign, ROUND(SUM(duration), 3) AS sec
      FROM lastheard
      WHERE callsign IS NOT NULL AND callsign <> '' AND duration IS NOT NULL
      GROUP BY UPPER(callsign)
      ORDER BY sec DESC
      LIMIT 10
    ")->fetchAll();

    foreach ($rows as &$r) {
      $r['country_code'] = prefix_to_country($r['callsign'] ?? null);
    }

    echo json_encode($rows, JSON_UNESCAPED_UNICODE);
    exit;
  }

  // Hall of Fame (kombinierter Score: 60% QSOs, 40% Total-Seconds)
  // QSOs mit Dauer < 5 s werden ignoriert
  if ($q === 'hallOfFame') {
    $hrs   = max(1, (int) ($_GET['since_h'] ?? 720));   // default 30 Tage
    $limit = max(1, min(50, (int) ($_GET['limit'] ?? 10)));

    $sql = "
      WITH base AS (
        SELECT UPPER(callsign) AS callsign,
               COUNT(*)        AS qso_count,
               SUM(duration)   AS total_sec,
               AVG(duration)   AS avg_sec
        FROM lastheard
        WHERE callsign IS NOT NULL
          AND callsign <> ''
          AND duration IS NOT NULL
          AND duration >= 15       -- ✅ hier: nur QSOs >= 15 s
          AND ts >= NOW() - INTERVAL :hrs HOUR
        GROUP BY UPPER(callsign)
      ),
      mx AS (
        SELECT MAX(qso_count) AS max_qso, MAX(total_sec) AS max_sec FROM base
      )
      SELECT
        b.callsign,
        b.qso_count,
        ROUND(b.total_sec, 3) AS total_sec,
        ROUND(b.avg_sec, 3)   AS avg_sec,
        ROUND(
          (CASE WHEN mx.max_qso > 0 THEN b.qso_count / mx.max_qso ELSE 0 END) * 60
          + (CASE WHEN mx.max_sec > 0 THEN b.total_sec / mx.max_sec ELSE 0 END) * 40
        , 3) AS score
      FROM base b CROSS JOIN mx
      ORDER BY score DESC, b.qso_count DESC, b.total_sec DESC
      LIMIT :lim
    ";

    $stmt = $pdo->prepare($sql);
    $stmt->bindValue(':hrs', $hrs, PDO::PARAM_INT);
    $stmt->bindValue(':lim', $limit, PDO::PARAM_INT);
    $stmt->execute();
    $rows = $stmt->fetchAll();

    foreach ($rows as &$r) {
      $r['country_code'] = prefix_to_country($r['callsign'] ?? null);
    }

    echo json_encode($rows, JSON_UNESCAPED_UNICODE);
    exit;
  }

  /* =========================
     Reflector-Status (D-Star/DMR/YSF)
     ========================= */
  if ($q === 'reflector') {
    $row = $pdo->query("
      SELECT
        dstar,
        dmr,
        fusion,
        DATE_FORMAT(updated_at, '%Y-%m-%d %H:%i:%s') AS updated_at
      FROM reflector
      WHERE id = 1
      LIMIT 1
    ")->fetch();

    if ($row) {
      foreach (['dstar', 'dmr', 'fusion'] as $k) {
        if ($row[$k] === null) {
          $row[$k] = '-----'; // Platzhalter
        }
      }
    }

    echo json_encode($row ?: [], JSON_UNESCAPED_UNICODE);
    exit;
  }

  // Fallback: unbekannter q-Parameter
  http_response_code(400);
  echo json_encode(['error' => 'bad query'], JSON_UNESCAPED_UNICODE);

} catch (Throwable $e) {
  // Generische Fehlerbehandlung (keine sensiblen Details leaken)
  http_response_code(500);
  echo json_encode(['error' => $e->getMessage()], JSON_UNESCAPED_UNICODE);
}
