//
//  HorizontalPullToRefreshTableViewController.h
//  HorizontalPullToRefreshTable
//
//  Created by Albert Nadal Garriga on 07/01/13.
//  Copyright (c) 2013 Albert Nadal Garriga. All rights reserved.
//

#import <UIKit/UIKit.h>
#import "SimplePullRefreshTableViewController.h"

@interface KPDUsersHorizontalTableViewController : SimplePullRefreshTableViewController <UIAlertViewDelegate>

- (id)initWithFrame:(CGRect)frame;
- (void)scrollContentToLeft;

@end
