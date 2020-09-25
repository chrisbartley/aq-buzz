//
//  FeatherAQDelegate.swift
//  AQ Buzz
//
//  Created by Chris Bartley on 9/6/20.
//  Copyright (c) 2020 Chris Bartley. Licensed under the MIT license. See LICENSE file.
//

import Foundation

public protocol FeatherAQDelegate: class {
   func featherAQ(_ featherAQ: FeatherAQ, areNotificationsEnabled: Bool, error: Error?)
   func featherAQ(_ featherAQ: FeatherAQ, rssi: NSNumber)
   func featherAQ(_ featherAQ: FeatherAQ, dataSample: FeatherAQ.DataSample)
   func featherAQ(_ featherAQ: FeatherAQ, errorGettingDataSample error: Error)
}
