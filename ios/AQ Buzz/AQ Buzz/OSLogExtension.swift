//
//  OSLogExtension.swift
//  AQ Buzz
//
//  Created by Chris Bartley on 9/6/20.
//  Copyright (c) 2020 Chris Bartley. Licensed under the MIT license. See LICENSE file.
//

import Foundation
import os

extension OSLog {
   static private let DEFAULT_BUNDLE_IDENTIFIER = "com.chrisbartley.aq-buzz"
   
   convenience init(category: String) {
      self.init(subsystem: Bundle.main.bundleIdentifier ?? OSLog.DEFAULT_BUNDLE_IDENTIFIER, category: category)
   }
}
