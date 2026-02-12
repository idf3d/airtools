CREATE TABLE `airQuality` (
  `pKey` bigint(20) NOT NULL,
  `ts` timestamp NOT NULL DEFAULT current_timestamp() ON UPDATE current_timestamp(),
  `sensor` text NOT NULL,
  `param` text NOT NULL,
  `value` decimal(10,2) NOT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

ALTER TABLE `airQuality`
  ADD PRIMARY KEY (`pKey`),
  ADD KEY `tsIndex` (`ts`),
  ADD KEY `sensor_index` (`sensor`(768)),
  ADD KEY `param_index` (`param`(768));

ALTER TABLE `airQuality`
  MODIFY `pKey` bigint(20) NOT NULL AUTO_INCREMENT;
