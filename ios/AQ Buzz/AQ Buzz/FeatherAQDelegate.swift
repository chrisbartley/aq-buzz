//
//  FeatherAQDelegate.swift
//  AQ Buzz
//
//  Created by Chris Bartley on 9/6/20.
//  Copyright © 2020 Chris Bartley. All rights reserved.
//

import Foundation

public protocol FeatherAQDelegate: class {
   func featherAQ(_ featherAQ: FeatherAQ, areNotificationsEnabled: Bool, error: Error?)
   func featherAQ(_ featherAQ: FeatherAQ, dataSample: FeatherAQ.DataSample)
   func featherAQ(_ featherAQ: FeatherAQ, errorGettingDataSample error: Error)
}
