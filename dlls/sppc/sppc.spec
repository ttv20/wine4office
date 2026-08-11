@ stub SLCallServer
@ stub SLpAuthenticateGenuineTicketResponse
@ stub SLpBeginGenuineTicketTransaction
@ stub SLpClearActivationInProgress
@ stub SLpDepositDownlevelGenuineTicket
@ stub SLpDepositTokenActivationResponse
@ stub SLpGenerateTokenActivationChallenge
@ stub SLpGetGenuineBlob
@ stub SLpGetGenuineLocal
@ stub SLpGetLicenseAcquisitionInfo
@ stub SLpGetMSPidInformation
@ stub SLpGetMachineUGUID
@ stub SLpGetTokenActivationGrantInfo
@ stub SLpIAActivateProduct
@ stub SLpIsCurrentInstalledProductKeyDefaultKey
@ stub SLpProcessVMPipeMessage
@ stub SLpSetActivationInProgress
@ stub SLpTriggerServiceWorker
@ stub SLpVLActivateProduct
@ cdecl __wine_sppc_set_auth_session_key(ptr long)
@ cdecl __wine_sppc_set_expected_hmac(ptr long)
@ stdcall SLClose(ptr)
@ stdcall SLConsumeRight(ptr ptr ptr wstr ptr)
@ stub SLDepositMigrationBlob
@ stub SLDepositOfflineConfirmationId
@ stub SLDepositOfflineConfirmationIdEx
@ stub SLDepositStoreToken
@ stub SLFireEvent
@ stub SLGatherMigrationBlob
@ stub SLGatherMigrationBlobEx
@ stub SLGenerateOfflineInstallationId
@ stub SLGenerateOfflineInstallationIdEx
@ stub SLGetActiveLicenseInfo
@ stub SLGetApplicationInformation
@ stdcall SLGetApplicationPolicy(ptr wstr ptr ptr ptr)
@ stdcall SLGetAuthenticationResult(ptr ptr ptr)
@ stub SLGetEncryptedPIDEx
@ stub SLGetGenuineInformation
@ stub SLGetInstalledProductKeyIds
@ stdcall SLGetLicense(ptr ptr ptr ptr)
@ stdcall SLGetLicenseFileId(ptr long ptr ptr)
@ stdcall SLGetLicenseInformation(ptr ptr wstr ptr ptr ptr)
@ stdcall SLGetLicensingStatusInformation(ptr ptr ptr wstr ptr ptr)
@ stub SLGetPKeyId
@ stdcall SLGetPKeyInformation(ptr ptr wstr ptr ptr ptr)
@ stdcall SLGetPolicyInformation(ptr wstr ptr ptr ptr)
@ stub SLGetPolicyInformationDWORD
@ stdcall SLGetProductSkuInformation(ptr ptr wstr ptr ptr ptr)
@ stdcall SLGetSLIDList(ptr long ptr long ptr ptr)
@ stdcall SLGetServiceInformation(ptr wstr ptr ptr ptr)
@ stdcall SLInstallLicense(ptr long ptr ptr)
@ stub SLInstallProofOfPurchase
@ stub SLInstallProofOfPurchaseEx
@ stub SLIsGenuineLocalEx
@ stdcall SLLoadApplicationPolicies(ptr ptr long ptr)
@ stdcall SLOpen(ptr)
@ stdcall SLPersistApplicationPolicies(ptr ptr long)
@ stub SLPersistRTSPayloadOverride
@ stub SLReArm
@ stub SLRegisterEvent
@ stub SLRegisterPlugin
@ stdcall SLSetAuthenticationData(ptr long ptr)
@ stub SLSetCurrentProductKey
@ stub SLSetGenuineInformation
@ stub SLUninstallLicense
@ stub SLUninstallProofOfPurchase
@ stdcall SLUnloadApplicationPolicies(ptr)
@ stub SLUnregisterEvent
@ stub SLUnregisterPlugin
