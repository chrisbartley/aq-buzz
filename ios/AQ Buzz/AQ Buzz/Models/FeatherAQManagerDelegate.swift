//
//  FeatherAQManagerDelegate.swift
//  AQ Buzz
//
//  Created by Chris Bartley on 9/6/20.
//  Copyright © 2020 Chris Bartley. All rights reserved.
//

import Foundation

public protocol FeatherAQManagerDelegate: class {
   func didUpdateState(_ featherAQManager: FeatherAQManager, to state: FeatherAQManagerManagerState)
   func didScanTimeout(_ featherAQManager: FeatherAQManager)
   func didDiscover(_ featherAQManager: FeatherAQManager, uuid: UUID, advertisementData: [String: Any], rssi: NSNumber, wasRediscovery: Bool)

   func didDisappear(_ featherAQManager: FeatherAQManager, uuid: UUID)
   func didConnectTo(_ featherAQManager: FeatherAQManager, uuid: UUID)
   func didDisconnectFrom(_ featherAQManager: FeatherAQManager, uuid: UUID, error: Error?)
   func didFailToConnectTo(_ featherAQManager: FeatherAQManager, uuid: UUID, error: Error?)
}
