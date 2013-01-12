//
//  ViewController.m
//  HorizontalPullToRefreshTable
//
//  Created by Albert Nadal Garriga on 07/01/13.
//  Copyright (c) 2013 Albert Nadal Garriga. All rights reserved.
//

#import "ViewController.h"
#import "KPDUsersHorizontalTableViewController.h"

@interface ViewController ()

@end

@implementation ViewController

- (void)viewDidLoad
{
    [super viewDidLoad];

    tvc = [[KPDUsersHorizontalTableViewController alloc] initWithFrame:CGRectMake(0, 0, 75, 320)];
    [self.view addSubview:tvc.view];
    [tvc scrollContentToLeft];

}

- (void)didReceiveMemoryWarning
{
    [super didReceiveMemoryWarning];
    // Dispose of any resources that can be recreated.
}

@end
