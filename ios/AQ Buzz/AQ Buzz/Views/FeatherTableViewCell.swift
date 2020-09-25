//
//  FeatherTableViewCell.swift
//  AQ Buzz
//
//  Created by Chris Bartley on 9/7/20.
//  Copyright (c) 2020 Chris Bartley. Licensed under the MIT license. See LICENSE file.
//

import UIKit

class FeatherTableViewCell: UITableViewCell {
   @IBOutlet public var nameLabel: UILabel!
   @IBOutlet public var tvocLabel: UILabel!
   @IBOutlet public var rssiLabel: UILabel!

   override public func awakeFromNib() {
      super.awakeFromNib()
   }

   override public func setSelected(_ selected: Bool, animated: Bool) {
      super.setSelected(selected, animated: animated)
   }
}
