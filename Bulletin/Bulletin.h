//
//  Bulletin.h
//  Bulletin
//
//  Created by Jeremy Tregunna on 2013-05-11.
//  Copyright (c) 2013 Jeremy Tregunna. All rights reserved.
//

#import <Foundation/Foundation.h>
#import "BulletinMessage.h"

@protocol BulletinDelegate;

@interface Bulletin : NSObject
@property (nonatomic, weak) id<BulletinDelegate> delegate;
@property (nonatomic, copy) NSString* username;
@property (nonatomic, copy) NSString* password;
@property (nonatomic) NSUInteger messageRetry;
@property (nonatomic) uint16_t keepAlive;
@property (nonatomic) BOOL cleanSession;

- (instancetype)initWithClientID:(NSString*)clientID delegate:(id<BulletinDelegate>)delegate;

- (void)connectToBrokerWithURL:(NSURL*)brokerURL;
- (void)reconnect;
- (void)disconnect;

- (void)publishMessage:(NSData*)payload topic:(NSString*)topic qualityOfService:(BulletinMessageServiceLevel)qos retained:(BOOL)retained;

- (void)subscribeToTopic:(NSString*)topic;
- (void)subscribeToTopic:(NSString*)topic qualityOfService:(BulletinMessageServiceLevel)qos;
- (void)unsubscribeFromTopic:(NSString*)topic;
@end

@protocol BulletinDelegate <NSObject>
@optional
- (void)bulletin:(Bulletin*)bulletin didConnectWithCode:(NSUInteger)code;
- (void)bulletinDidDisconnect:(Bulletin*)bulletin;
- (void)bulletin:(Bulletin*)bulletin didPublishMessageWithID:(NSUInteger)messageID;
- (void)bulletin:(Bulletin*)bulletin didReceiveMessage:(BulletinMessage*)msg;
- (void)bulletin:(Bulletin*)bulletin didSubscribeMessageID:(NSUInteger)messageID qualityOfService:(BulletinMessageServiceLevel)qos;
- (void)bulletin:(Bulletin*)bulletin didUnsubscribeMessageID:(NSUInteger)messageID;
@end
