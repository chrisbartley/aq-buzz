//
//  ViewController.swift
//  AQ Buzz
//
//  Created by Chris Bartley on 9/6/20.
//  Copyright © 2020 Chris Bartley. All rights reserved.
//

import UIKit
import CoreBluetooth
import BuzzBLE

class ViewController: UIViewController {
   @IBOutlet var buzzScanningStackView: UIStackView!
   @IBOutlet var queryingDeviceStackView: UIStackView!
   @IBOutlet var buzzMainStackView: UIStackView!

   @IBOutlet var featherScanningStackView: UIStackView!
   @IBOutlet var featherMainStackView: UIStackView!

   @IBOutlet var buzzIdLabel: UILabel!
   @IBOutlet var batteryLabel: UILabel!

   @IBOutlet var featherNameLabel: UILabel!
   @IBOutlet var tvocLabel: UILabel!
   @IBOutlet var rssiLabel: UILabel!

   private let buzzManager = BuzzManager()
   private let featherManager = FeatherAQManager()

   private var buzz: Buzz?
   private var feather: FeatherAQ?

   override func viewDidLoad() {
      super.viewDidLoad()

      buzzManager.delegate = self
      featherManager.delegate = self
   }
}

extension ViewController: BuzzManagerDelegate {
   private func scanForBuzz() {
      buzzScanningStackView.isHidden = false
      queryingDeviceStackView.isHidden = true
      buzzMainStackView.isHidden = true

      if buzzManager.startScanning(timeoutSecs: -1, assumeDisappearanceAfter: 1) {
         print("Scanning for Buzz devices started!")
      } else {
         // TODO:
         print("Failed to start scanning for Buzz devices.")
      }
   }

   func didUpdateState(_ buzzManager: BuzzManager, to state: BuzzManagerState) {
      print("BuzzManagerDelegate.didUpdateState: \(state)")
      if state == .enabled {
         scanForBuzz()
      }
   }

   func didDiscover(_ buzzManager: BuzzManager, uuid: UUID, advertisementData: [String: Any], rssi: NSNumber) {
      if buzzManager.connectToBuzz(havingUUID: uuid) {
         print("BuzzManagerDelegate.didDiscover: uuid=\(uuid), attempting to connect...")
      } else {
         print("BuzzManagerDelegate.didDiscover: Cannot connect to buzz with uuid \(uuid)!")
      }
   }

   func didRediscover(_ buzzManager: BuzzManager, uuid: UUID, advertisementData: [String: Any], rssi: NSNumber) {
      // print("BuzzManagerDelegate.didRediscover: uuid=\(uuid)")
   }

   func didDisappear(_ buzzManager: BuzzManager, uuid: UUID) {
      print("BuzzManagerDelegate.didDisappear: uuid=\(uuid)")
   }

   func didConnectTo(_ buzzManager: BuzzManager, uuid: UUID) {
      if let buzz = buzzManager.getBuzz(uuid: uuid) {
         print("BuzzManagerDelegate.didConnectTo: uuid=\(uuid)")

         // stop scanning
         if buzzManager.stopScanning() {
            print("BuzzManagerDelegate: Scanning stopped")
         } else {
            print("BuzzManagerDelegate: Failed to stop scanning!")
         }

         self.buzz = buzz

         // register self as delegate and enable communication
         buzz.delegate = self
         buzz.enableCommuication()

         DispatchQueue.main.async {
            self.buzzScanningStackView.isHidden = true
            self.queryingDeviceStackView.isHidden = false
            self.buzzMainStackView.isHidden = true
         }
      } else {
         print("BuzzManagerDelegate.didConnectTo: received didConnectTo, but buzzManager doesn't recognize UUID \(uuid)")
      }
   }

   func didDisconnectFrom(_ buzzManager: BuzzManager, uuid: UUID, error: Error?) {
      print("BuzzManagerDelegate.didDisconnectFrom: uuid=\(uuid)")

      buzz = nil

      scanForBuzz()
   }

   func didFailToConnectTo(_ buzzManager: BuzzManager, uuid: UUID, error: Error?) {
      print("BuzzManagerDelegate.didFailToConnectTo: uuid=\(uuid)")
   }
}

extension ViewController: FeatherAQManagerDelegate {
   private func scanForFeather() {
      featherScanningStackView.isHidden = false
      featherMainStackView.isHidden = true

      if featherManager.startScanning(timeoutSecs: -1, assumeDisappearanceAfter: 1) {
         print("Scanning for Feather AQ devices started!")
      } else {
         // TODO:
         print("Failed to start scanning for Feather AQ devices.")
      }
   }

   func didUpdateState(_ featherAQManager: FeatherAQManager, to state: FeatherAQManagerManagerState) {
      if state == .enabled {
         scanForFeather()
      } else {
         // TODO:
         print("FeatherAQManagerDelegate.didUpdateState: \(state)")
      }
   }

   func didScanTimeout(_ featherAQManager: FeatherAQManager) {
      print("FeatherAQManagerDelegate.didScanTimeout")
   }

   func didDiscover(_ featherAQManager: FeatherAQManager, uuid: UUID, advertisementData: [String: Any], rssi: NSNumber, wasRediscovery: Bool) {
      if featherManager.connectToFeatherAQ(havingUUID: uuid) {
         print("FeatherAQManagerDelegate.didDiscover: uuid=\(uuid), attempting to connect...")
      } else {
         print("Cannot connect!")
      }
   }

   func didDisappear(_ featherAQManager: FeatherAQManager, uuid: UUID) {
      print("FeatherAQManagerDelegate.didDisappear: uuid=\(uuid)")
   }

   func didConnectTo(_ featherAQManager: FeatherAQManager, uuid: UUID) {
      if let feather = featherManager.getFeatherAQ(uuid: uuid) {
         print("FeatherAQManagerDelegate.didConnectTo: uuid=\(uuid)")

         // stop scanning
         if featherManager.stopScanning() {
            print("FeatherAQManagerDelegate: Scanning stopped")
         } else {
            print("FeatherAQManagerDelegate: Failed to stop scanning!")
         }

         self.feather = feather
         self.feather?.enableRSSIUpdates()

         // register self as delegate and enable notifications
         feather.delegate = self
         feather.enableNotifications()

         DispatchQueue.main.async {
            self.featherNameLabel.text = feather.name ?? "FeatherAQ"
            self.featherScanningStackView.isHidden = true
            self.featherMainStackView.isHidden = false
         }
      } else {
         print("FeatherAQManagerDelegate.didConnectTo: received didConnectTo, but featherManager doesn't recognize UUID \(uuid)")
      }
   }

   func didDisconnectFrom(_ featherAQManager: FeatherAQManager, uuid: UUID, error: Error?) {
      print("FeatherAQManagerDelegate.didDisconnectFrom: uuid=\(uuid)")

      feather = nil

      scanForFeather()
   }

   func didFailToConnectTo(_ featherAQManager: FeatherAQManager, uuid: UUID, error: Error?) {
      print("FeatherAQManagerDelegate.didFailToConnectTo: uuid=\(uuid)")
   }
}

extension ViewController: BuzzDelegate {
   func buzz(_ buzz: Buzz, isCommunicationEnabled: Bool, error: Error?) {
      if let error = error {
         print("BuzzDelegate.isCommunicationEnabled: \(isCommunicationEnabled), error: \(error))")
      } else {
         if isCommunicationEnabled {
            print("BuzzDelegate.isCommunicationEnabled: communication enabled, requesting device and battery info and then authorizing...")
            buzz.requestBatteryInfo() // TODO: Add a timer to update battery level periodically
            buzz.requestDeviceInfo()
            buzz.authorize()
         } else {
            // TODO:
            print("BuzzDelegate.isCommunicationEnabled: failed to enable communication. Um...darn.")
         }
      }
   }

   func buzz(_ buzz: Buzz, isAuthorized: Bool, errorMessage: String?) {
      if isAuthorized {
         // now that we're authorized, disable the mic, enable motors, and stop the motors
         buzz.disableMic()
         buzz.enableMotors()
         buzz.clearMotorsQueue()
      } else {
         // TODO:
         print("Failed to authorize: \(String(describing: errorMessage))")
      }
   }

   func buzz(_ buzz: Buzz, batteryInfo: Buzz.BatteryInfo) {
      print("BuzzDelegate.batteryInfo: \(batteryInfo)")
      DispatchQueue.main.async {
         self.batteryLabel.text = "\(batteryInfo.level)%"
      }
   }

   func buzz(_ buzz: Buzz, deviceInfo: Buzz.DeviceInfo) {
      print("BuzzDelegate.deviceInfo: \(deviceInfo)")

      DispatchQueue.main.async {
         self.buzzIdLabel.text = "Buzz \(deviceInfo.id)"

         self.queryingDeviceStackView.isHidden = true
         self.buzzMainStackView.isHidden = false
      }
   }

   func buzz(_ buzz: Buzz, isMicEnabled: Bool) {
      print("BuzzDelegate.isMicEnabled: \(isMicEnabled)")
   }

   func buzz(_ buzz: Buzz, areMotorsEnabled: Bool) {
      print("BuzzDelegate.areMotorsEnabled: \(areMotorsEnabled)")
   }

   func buzz(_ buzz: Buzz, isMotorsQueueCleared: Bool) {
      print("BuzzDelegate.isMotorsQueueCleared: \(isMotorsQueueCleared)")
   }

   func buzz(_ buzz: Buzz, responseError error: Error) {
      print("BuzzDelegate.responseError: \(error)")
   }

   func buzz(_ buzz: Buzz, unknownCommand command: String) {
      print("BuzzDelegate.unknownCommand: \(command) length (\(command.count))")
   }

   func buzz(_ buzz: Buzz, badRequestFor command: Buzz.Command, errorMessage: String?) {
      print("BuzzDelegate.badRequestFor: \(command), error: \(String(describing: errorMessage))")
   }

   func buzz(_ buzz: Buzz, failedToParse responseMessage: String, forCommand command: Buzz.Command) {
      print("BuzzDelegate.failedToParse: \(responseMessage) forCommand \(command)")
   }
}

extension ViewController: FeatherAQDelegate {
   func featherAQ(_ featherAQ: FeatherAQ, areNotificationsEnabled: Bool, error: Error?) {
      if let error = error {
         print("FeatherAQDelegate.areNotificationsEnabled: failed to change notification state (\(areNotificationsEnabled)) due to error: \(error)")
      } else {
         print("FeatherAQDelegate.areNotificationsEnabled: notifications \(areNotificationsEnabled ? "en" : "dis")abled!")
      }
   }

   func featherAQ(_ featherAQ: FeatherAQ, rssi: NSNumber) {
      DispatchQueue.main.async {
         self.rssiLabel.text = "\(rssi)"
      }
   }

   func featherAQ(_ featherAQ: FeatherAQ, dataSample: FeatherAQ.DataSample) {
      DispatchQueue.main.async {
         self.tvocLabel.text = "\(dataSample.avgTvoc) ppb"
      }
   }

   func featherAQ(_ featherAQ: FeatherAQ, errorGettingDataSample error: Error) {
      // TODO:
   }
}
