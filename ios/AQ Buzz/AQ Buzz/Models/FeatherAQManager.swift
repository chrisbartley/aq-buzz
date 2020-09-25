//
//  FeatherAQManager.swift
//  AQ Buzz
//
//  Created by Chris Bartley on 9/6/20.
//  Copyright (c) 2020 Chris Bartley. Licensed under the MIT license. See LICENSE file.
//

import Foundation
import os
import CoreBluetooth
import BirdbrainBLE

public enum FeatherAQManagerManagerState: String {
   case enabled = "Enabled"
   case disabled = "Disabled"
   case error = "Error"
}

fileprivate extension OSLog {
   static let log = OSLog(category: "FeatherAQManager")
}

public class FeatherAQManager {
   // MARK: - Public Properties

   public weak var delegate: FeatherAQManagerDelegate?

   /// Returns the number of connected Buzz devices
   public var connectedDeviceCount: Int {
      connectedDevices.count
   }

   // MARK: - Private Properties

   private let bleCentralManager: StandardBLECentralManager

   private var connectedDevices = [UUID: FeatherAQ]()

   // MARK: - Initializers

   public convenience init(delegate: FeatherAQManagerDelegate) {
      self.init()
      self.delegate = delegate
   }

   public init() {
      bleCentralManager = StandardBLECentralManager(servicesAndCharacteristics: FeatherAQ.ServicesAndCharacteristics.instance)
      bleCentralManager.delegate = self
   }

   // MARK: - Public Methods

   @discardableResult
   public func startScanning(timeoutSecs: TimeInterval = -1,
                             assumeDisappearanceAfter: TimeInterval = StandardBLECentralManager.defaultAssumeDisappearanceTimeInterval) -> Bool {
      return bleCentralManager.startScanning(timeoutSecs: timeoutSecs, assumeDisappearanceAfter: assumeDisappearanceAfter)
   }

   @discardableResult
   public func stopScanning() -> Bool {
      return bleCentralManager.stopScanning()
   }

   @discardableResult
   public func connectToFeatherAQ(havingUUID uuid: UUID) -> Bool {
      return bleCentralManager.connectToPeripheral(havingUUID: uuid)
   }

   @discardableResult
   public func disconnectFromFeatherAQ(havingUUID uuid: UUID) -> Bool {
      return bleCentralManager.disconnectFromPeripheral(havingUUID: uuid)
   }

   public func getFeatherAQ(uuid: UUID) -> FeatherAQ? {
      return connectedDevices[uuid]
   }
}

extension FeatherAQManager: BLECentralManagerDelegate {
   public func didUpdateState(to state: CBManagerState) {
      os_log("didUpdateState(%{public}s)", log: OSLog.log, type: .debug, String(describing: state))
      switch state {
         case .poweredOn:
            delegate?.didUpdateState(self, to: .enabled)
         case .poweredOff:
            delegate?.didUpdateState(self, to: .disabled)
         case .unauthorized, .unknown, .resetting, .unsupported:
            delegate?.didUpdateState(self, to: .error)
         @unknown default:
            os_log("A previously unknown central manager state occurred. CBManagerState '%{public}s' not yet handled", log: OSLog.log, type: .error, String(describing: state))
            delegate?.didUpdateState(self, to: .error)
      }
   }

   public func didPowerOn() {
      // nothing to do, handled by didUpdateState()
   }

   public func didPowerOff() {
      // nothing to do, handled by didUpdateState()
   }

   public func didScanTimeout() {
      delegate?.didScanTimeout(self)
   }

   public func didDiscoverPeripheral(uuid: UUID, advertisementData: [String: Any], rssi: NSNumber) {
      delegate?.didDiscover(self, uuid: uuid,
                            advertisementData: advertisementData,
                            rssi: rssi, wasRediscovery: false)
   }

   public func didRediscoverPeripheral(uuid: UUID, advertisementData: [String: Any], rssi: NSNumber) {
      delegate?.didDiscover(self, uuid: uuid,
                              advertisementData: advertisementData,
                              rssi: rssi, wasRediscovery: true)
   }

   public func didPeripheralDisappear(uuid: UUID) {
      delegate?.didDisappear(self, uuid: uuid)
   }

   public func didConnectToPeripheral(peripheral: BLEPeripheral) {
      connectedDevices[peripheral.uuid] = FeatherAQ(blePeripheral: peripheral)
      delegate?.didConnectTo(self, uuid: peripheral.uuid)
   }

   public func didDisconnectFromPeripheral(uuid: UUID, error: Error?) {
      if let feather = connectedDevices.removeValue(forKey: uuid) {
         feather.disableRSSIUpdates()
         delegate?.didDisconnectFrom(self, uuid: uuid, error: error)
      }
   }

   public func didFailToConnectToPeripheral(uuid: UUID, error: Error?) {
      delegate?.didFailToConnectTo(self, uuid: uuid, error: error)
   }
}
