//
//  FeatherAQ.swift
//  AQ Buzz
//
//  Created by Chris Bartley on 9/6/20.
//  Copyright © 2020 Chris Bartley. All rights reserved.
//

import Foundation
import os
import CoreBluetooth
import BirdbrainBLE

private extension OSLog {
   static let log = OSLog(category: "FeatherAQ")
}

public class FeatherAQ {
   class ServicesAndCharacteristics: SupportedServicesAndCharacteristics {
      public static let instance = ServicesAndCharacteristics()

      public static let serviceUUID = CBUUID(string: "42610001-7274-6c65-7946-656174686572")
      public static let characteristicUUID = CBUUID(string: "42610002-7274-6c65-7946-656174686572")

      private static let serviceAndCharacteristicUUIDs: [CBUUID: [CBUUID]] = [serviceUUID: [characteristicUUID]]

      public var serviceUUIDs: [CBUUID] {
         Array(ServicesAndCharacteristics.serviceAndCharacteristicUUIDs.keys)
      }

      public func characteristicUUIDs(belongingToService service: CBService) -> [CBUUID]? {
         ServicesAndCharacteristics.serviceAndCharacteristicUUIDs[service.uuid]
      }

      private init() {}
   }

   public struct DataSample {
      let unixTimeSeconds: TimeInterval
      let tvoc: UInt16
      let avgTvoc: Float
      let slope: Float

      fileprivate init(data: Data) {
         let timestampFeather: UInt64 = data[0...7].reduce(0) { soFar, byte in
            soFar << 8 | UInt64(byte)
         }
         // Feathers with no RTC will report a 0 timestamp, so default to just using the central's timestamp
         unixTimeSeconds = (timestampFeather == 0) ? Date().timeIntervalSince1970 : TimeInterval(timestampFeather.byteSwapped)

         let tvocFeather: UInt16 = data[8...9].reduce(0) { soFar, byte in
            soFar << 8 | UInt16(byte)
         }
         tvoc = tvocFeather.byteSwapped

         // found this conversion technique at https://stackoverflow.com/a/41163620/703200
         avgTvoc = Float(bitPattern: UInt32(bigEndian: data[10...13].reversed().withUnsafeBytes { $0.load(as: UInt32.self) }))
         slope = Float(bitPattern: UInt32(bigEndian: data[14...17].reversed().withUnsafeBytes { $0.load(as: UInt32.self) }))
      }
   }

   // MARK: - Public Properties

   public var uuid: UUID {
      blePeripheral.uuid
   }

   public var name: String? {
      blePeripheral.name
   }

   public weak var delegate: FeatherAQDelegate?

   // MARK: - Private Properties

   private let blePeripheral: BLEPeripheral

   private var mtu: Int {
      blePeripheral.maximumWriteWithoutResponseDataLength()
   }

   private var rssiTimer: Timer?

   // MARK: - Initializers

   public required init(blePeripheral: BLEPeripheral) {
      self.blePeripheral = blePeripheral

      // set self as delegate
      self.blePeripheral.delegate = self
   }

   deinit {
      // stop the timer, if necessary
      disableRSSIUpdates()
   }

   // MARK: - Public Methods

   public func enableNotifications() {
      _ = blePeripheral.setNotifyEnabled(onCharacteristic: ServicesAndCharacteristics.characteristicUUID)
   }

   public func disableNotifications() {
      _ = blePeripheral.setNotifyDisabled(onCharacteristic: ServicesAndCharacteristics.characteristicUUID)
   }

   public func enableRSSIUpdates() {
      // don't do anything if we're already doing RSSI updates (i.e. we have a Timer)
      if rssiTimer == nil {
         rssiTimer = Timer.scheduledTimer(withTimeInterval: 1.0, repeats: true) { _ in
            self.blePeripheral.readRSSI()
         }
      }
   }

   public func disableRSSIUpdates() {
      if let rssiTimer = rssiTimer {
         rssiTimer.invalidate()
         self.rssiTimer = nil
      }
   }
}

extension FeatherAQ: BLEPeripheralDelegate {
   public func blePeripheral(_ peripheral: BLEPeripheral, didUpdateNotificationStateFor characteristicUUID: CBUUID, isNotifying: Bool, error: Error?) {
      delegate?.featherAQ(self, areNotificationsEnabled: isNotifying, error: error)
   }

   public func blePeripheral(_ peripheral: BLEPeripheral, didUpdateValueFor characteristicUUID: CBUUID, value: Data?, error: Error?) {
      if let error = error {
         os_log("BLEPeripheralDelegate.didUpdateValueFor uuid=%s, error=%s", log: OSLog.log, type: .error, characteristicUUID.uuidString, String(describing: error))
         delegate?.featherAQ(self, errorGettingDataSample: error)
      } else {
         if let data = value {
            delegate?.featherAQ(self, dataSample: DataSample(data: data))
         } else {}
      }
   }

   public func blePeripheral(_ peripheral: BLEPeripheral, didReadRSSI rssi: NSNumber) {
      delegate?.featherAQ(self, rssi: rssi)
   }
}
