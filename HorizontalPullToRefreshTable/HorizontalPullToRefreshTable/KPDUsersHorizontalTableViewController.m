//
//  HorizontalPullToRefreshTableViewController.m
//  HorizontalPullToRefreshTable
//
//  Created by Albert Nadal Garriga on 07/01/13.
//  Copyright (c) 2013 Albert Nadal Garriga. All rights reserved.
//

#import "KPDUsersHorizontalTableViewController.h"

@interface KPDUsersHorizontalTableViewController ()

@end

@implementation KPDUsersHorizontalTableViewController

- (id)initWithFrame:(CGRect)frame
{
    self = [super initWithStyle:UITableViewStylePlain];
    if (self)
    {
        int x = (frame.origin.x - (frame.size.width / 2)) + (frame.size.height / 2);
        int y = (frame.origin.y - (frame.size.height / 2)) + (frame.size.width / 2);

        self.view = [[UITableView alloc] initWithFrame:CGRectMake(x, y, frame.size.width, frame.size.height) style:UITableViewStylePlain];

        [self addPullToRefreshHeader];
        [(UITableView *)self.view setDataSource:self];
        [(UITableView *)self.view setDelegate:self];
        [(UITableView *)self.view setShowsHorizontalScrollIndicator:FALSE];
        [(UITableView *)self.view setShowsVerticalScrollIndicator:FALSE];
        [(UITableView *)self.view setSeparatorStyle:UITableViewCellSeparatorStyleNone];
        ((UITableView *)self.view).transform = CGAffineTransformMakeRotation(M_PI * 0.5);
    }

    return self;
}

- (id)initWithStyle:(UITableViewStyle)style
{
    self = [super initWithStyle:style];
    if (self) {
        // Custom initialization
    }
    return self;
}

- (void)scrollContentToLeft
{
    NSIndexPath* ipath = [NSIndexPath indexPathForRow:14 inSection:0];
    [(UITableView *)self.view scrollToRowAtIndexPath:ipath atScrollPosition:UITableViewScrollPositionBottom animated: NO];
}

- (void)didReceiveMemoryWarning
{
    [super didReceiveMemoryWarning];
    // Dispose of any resources that can be recreated.
}

- (void)refresh
{
    [self performSelector:@selector(showAlert) withObject:nil afterDelay:0.5];
}

- (void)alertView:(UIAlertView *)alertView clickedButtonAtIndex:(NSInteger)buttonIndex
{
    [self stopLoading];
}

- (void)showAlert
{
    UIAlertView* alertView = [[UIAlertView alloc] initWithTitle:@"Refresh" message:@"It seems works!" delegate:self cancelButtonTitle:@"OK" otherButtonTitles:nil];
    [alertView show];
}

#pragma mark - Table view data source

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView
{
    // Return the number of sections.
    return 1;
}

- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section
{
    // Return the number of rows in the section.
    return 15;
}

- (CGFloat)tableView:(UITableView *)tableView heightForRowAtIndexPath:(NSIndexPath *)indexPath
{
    return self.view.frame.size.height;
}

- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)indexPath
{
    static NSString *CellIdentifier = @"MyCell";

    UITableViewCell *cell = [tableView dequeueReusableCellWithIdentifier:CellIdentifier];

    if (!cell)
    {
        cell = [[UITableViewCell alloc]
                initWithStyle: UITableViewCellStyleDefault
                reuseIdentifier: CellIdentifier];

        UIImageView *imatge = [[UIImageView alloc] initWithFrame:CGRectMake(0,0,self.view.frame.size.height,self.view.frame.size.height)];
        [imatge setImage:[UIImage imageNamed:@"donald.png"]];
        [cell.contentView addSubview:imatge];
        [cell.contentView setBackgroundColor:[UIColor grayColor]];

        cell.transform = CGAffineTransformMakeRotation(M_PI * -0.5);
    }

    return cell;
}

#pragma mark - Table view delegate

- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)indexPath
{
    
}

@end
