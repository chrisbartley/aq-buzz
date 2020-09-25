//
//  ViewController.swift
//  AQ Buzz
//
//  Created by Chris Bartley on 9/6/20.
//  Copyright (c) 2020 Chris Bartley. Licensed under the MIT license. See LICENSE file.
//

import UIKit
import CoreBluetooth
import BuzzBLE

struct ConnectedFeather {
   let uuid: UUID
   let name: String
   let avgTvoc: Float
   let slope: Float
   let rssi: NSNumber

   init(uuid: UUID, name: String, avgTvoc: Float = 0.0, slope: Float = 0.0, rssi: NSNumber = 0) {
      self.uuid = uuid
      self.name = name
      self.avgTvoc = avgTvoc
      self.slope = slope
      self.rssi = rssi
   }
}

class ViewController: UIViewController {
   private static let maxBuzzIntensity: UInt8 = 255

   @IBOutlet var buzzScanningStackView: UIStackView!
   @IBOutlet var queryingDeviceStackView: UIStackView!
   @IBOutlet var buzzMainStackView: UIStackView!

   @IBOutlet var featherScanningStackView: UIStackView!
   @IBOutlet var featherTableView: UITableView!

   @IBOutlet var buzzIdLabel: UILabel!
   @IBOutlet var batteryLabel: UILabel!

   private let buzzManager = BuzzManager()
   private let featherManager = FeatherAQManager()

   private var buzz: Buzz?
   private var feathers = [ConnectedFeather]()

   // TODO: let user change this in the UI, and maybe the min, too
   private let maxVoc: Float = 300.0

   override func viewDidLoad() {
      super.viewDidLoad()

      featherTableView.dataSource = self

      buzzManager.delegate = self
      featherManager.delegate = self
   }

   private func updateFeathersTable() {
      // sort by RSSI, so that nearest is first
      feathers.sort { $0.rssi.compare($1.rssi) == .orderedDescending }

      DispatchQueue.main.async {
         self.featherTableView.reloadData()
      }
   }
}

// MARK: - UITableViewDataSource

extension ViewController: UITableViewDataSource {
   func numberOfSections(in tableView: UITableView) -> Int {
      return 1
   }

   func tableView(_ tableView: UITableView, numberOfRowsInSection section: Int) -> Int {
      return feathers.count
   }

   func tableView(_ tableView: UITableView, cellForRowAt indexPath: IndexPath) -> UITableViewCell {
      // Table view cells are reused and should be dequeued using a cell identifier.
      let cellIdentifier = "FeatherTableViewCell"

      guard let cell = tableView.dequeueReusableCell(withIdentifier: cellIdentifier, for: indexPath) as? FeatherTableViewCell else {
         fatalError("The dequeued cell is not an instance of FeatherTableViewCell.")
      }

      let feather = feathers[indexPath.row]

      cell.nameLabel.text = feather.name
      cell.tvocLabel.text = "\(feather.avgTvoc)"
      cell.rssiLabel.text = "RSSI: \(feather.rssi)"

      return cell
   }
}

// MARK: - BuzzManagerDelegate

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

   private func updateBuzzVibration() {
      // nothing to do if no feathers are connected
      if !feathers.isEmpty {
         // make sure we have a Buzz connected
         if let buzz = buzz {
            // count on the RSSI sorting in updateFeathersTable() to ensure
            // that the nearest feather is the first one in the feathers array
            let nearestFeather = feathers[0]

            // compute intensities then send to the Buzz
            let intensity: UInt8 = UInt8((max(0, min(nearestFeather.avgTvoc, maxVoc)) / maxVoc) * Float(ViewController.maxBuzzIntensity))

            var intensities: [UInt8] = [0, 0, 0, 0]

            if nearestFeather.slope > 0.414 {
               intensities[2] = intensity / 2
               intensities[3] = intensity
            } else if nearestFeather.slope < -0.414 {
               intensities[0] = intensity
               intensities[1] = intensity / 2
            } else {
               intensities[1] = intensity
               intensities[2] = intensity
            }

            buzz.sendMotorsCommand(data: intensities)
         }
      }
   }
}

// MARK: - FeatherAQManagerDelegate

extension ViewController: FeatherAQManagerDelegate {
   private func scanForFeather() {
      featherScanningStackView.isHidden = false
      featherTableView.isHidden = feathers.isEmpty

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

         // register self as delegate and enable notifications
         feather.delegate = self
         feather.enableNotifications()

         // add the new feather to the table
         feathers.append(ConnectedFeather(uuid: feather.uuid,
                                          name: feather.name ?? "FeatherAQ"))

         // turn on RSSI updates
         feather.enableRSSIUpdates()

         DispatchQueue.main.async {
            self.featherScanningStackView.isHidden = true
            self.featherTableView.isHidden = self.feathers.isEmpty
            self.updateFeathersTable()
         }
      } else {
         print("FeatherAQManagerDelegate.didConnectTo: received didConnectTo, but featherManager doesn't recognize UUID \(uuid)")
      }
   }

   func didDisconnectFrom(_ featherAQManager: FeatherAQManager, uuid: UUID, error: Error?) {
      print("FeatherAQManagerDelegate.didDisconnectFrom: uuid=\(uuid)")

      feathers.removeAll(where: { (feather) -> Bool in
         feather.uuid == uuid
      })
      updateFeathersTable()

      // if no feathers are left, then stop the motor vibration
      if feathers.isEmpty {
         if let buzz = buzz {
            buzz.stopMotors()
         }
      }

      scanForFeather()
   }

   func didFailToConnectTo(_ featherAQManager: FeatherAQManager, uuid: UUID, error: Error?) {
      print("FeatherAQManagerDelegate.didFailToConnectTo: uuid=\(uuid)")
   }
}

// MARK: - BuzzDelegate

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

// MARK: - FeatherAQDelegate

extension ViewController: FeatherAQDelegate {
   func featherAQ(_ featherAQ: FeatherAQ, areNotificationsEnabled: Bool, error: Error?) {
      if let error = error {
         print("FeatherAQDelegate.areNotificationsEnabled: failed to change notification state (\(areNotificationsEnabled)) due to error: \(error)")
      } else {
         print("FeatherAQDelegate.areNotificationsEnabled: notifications \(areNotificationsEnabled ? "en" : "dis")abled!")
      }
   }

   func featherAQ(_ featherAQ: FeatherAQ, rssi: NSNumber) {
      // find this feather in the collection, then update its RSSI
      for i in 0..<feathers.count {
         let feather = feathers[i]
         if feather.uuid == featherAQ.uuid {
            feathers[i] = ConnectedFeather(uuid: feather.uuid,
                                           name: feather.name,
                                           avgTvoc: feather.avgTvoc,
                                           slope: feather.slope,
                                           rssi: rssi)
            updateFeathersTable()
            break
         }
      }
   }

   func featherAQ(_ featherAQ: FeatherAQ, dataSample: FeatherAQ.DataSample) {
      // find this feather in the collection, then update its tVOC
      for i in 0..<feathers.count {
         let feather = feathers[i]
         if feather.uuid == featherAQ.uuid {
            feathers[i] = ConnectedFeather(uuid: feather.uuid,
                                           name: feather.name,
                                           avgTvoc: dataSample.avgTvoc,
                                           slope: feather.slope,
                                           rssi: feather.rssi)
            updateFeathersTable()
            break
         }
      }

      // update the Buzz vibration for the nearest feather
      updateBuzzVibration()
   }

   func featherAQ(_ featherAQ: FeatherAQ, errorGettingDataSample error: Error) {
      // TODO:
   }
}
